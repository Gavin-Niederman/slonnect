#include "device.h"
#include "kdeconnect.h"

#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

static const QString BatteryIface = QStringLiteral("org.kde.kdeconnect.device.battery");

Device::Device(const QString &id, QObject *parent)
    : QObject(parent)
    , m_id(id)
{
    auto bus = kdc::bus();
    for (const char *sig : {"reachableChanged", "nameChanged", "pairStateChanged", "typeChanged", "linksChanged"})
        bus.connect(kdc::Service, path(), kdc::DeviceIface, QLatin1String(sig), this, SLOT(refresh()));
    bus.connect(kdc::Service, path(), kdc::DeviceIface, QStringLiteral("pluginsChanged"), this, SLOT(refreshPlugins()));
    refresh();
    refreshPlugins();
}

Device::~Device()
{
    auto bus = kdc::bus();
    for (const char *sig : {"reachableChanged", "nameChanged", "pairStateChanged", "typeChanged", "linksChanged"})
        bus.disconnect(kdc::Service, path(), kdc::DeviceIface, QLatin1String(sig), this, SLOT(refresh()));
    bus.disconnect(kdc::Service, path(), kdc::DeviceIface, QStringLiteral("pluginsChanged"), this, SLOT(refreshPlugins()));
    connectBattery(false);
}

QString Device::path() const
{
    return kdc::devicePath(m_id);
}

void Device::refresh()
{
    kdc::getAll(path(), kdc::DeviceIface, this, [this](const QVariantMap &p) {
        m_name = p.value(QStringLiteral("name")).toString();
        m_type = p.value(QStringLiteral("type")).toString();
        m_reachable = p.value(QStringLiteral("isReachable")).toBool();
        m_paired = p.value(QStringLiteral("isPaired")).toBool();
        m_pairRequested = p.value(QStringLiteral("isPairRequested")).toBool();
        m_pairRequestedByPeer = p.value(QStringLiteral("isPairRequestedByPeer")).toBool();
        m_verificationKey = p.value(QStringLiteral("verificationKey")).toString();
        m_addresses = p.value(QStringLiteral("reachableAddresses")).toStringList();
        Q_EMIT changed();
    });
}

void Device::refreshPlugins()
{
    kdc::call(path(), kdc::DeviceIface, QStringLiteral("loadedPlugins"), {}, this, [this](const QDBusMessage &reply) {
        QStringList plugins = kdc::unwrap(reply.arguments().value(0)).toStringList();
        plugins.sort();
        if (plugins != m_loadedPlugins) {
            m_loadedPlugins = plugins;
            connectBattery(hasPlugin(QStringLiteral("kdeconnect_battery")));
            Q_EMIT pluginsChanged();
        }
    });
}

void Device::connectBattery(bool on)
{
    if (on == m_batteryConnected)
        return;
    m_batteryConnected = on;
    const QString batteryPath = path() + QStringLiteral("/battery");
    if (on) {
        kdc::bus().connect(kdc::Service, batteryPath, BatteryIface, QStringLiteral("refreshed"), this, SLOT(onBatteryRefreshed(bool, int)));
        refreshBattery();
    } else {
        kdc::bus().disconnect(kdc::Service, batteryPath, BatteryIface, QStringLiteral("refreshed"), this, SLOT(onBatteryRefreshed(bool, int)));
        m_hasBattery = false;
        Q_EMIT batteryChanged();
    }
}

void Device::refreshBattery()
{
    kdc::getAll(path() + QStringLiteral("/battery"), BatteryIface, this, [this](const QVariantMap &p) {
        m_hasBattery = p.value(QStringLiteral("hasBattery")).toBool();
        m_charge = p.value(QStringLiteral("charge"), -1).toInt();
        m_charging = p.value(QStringLiteral("isCharging")).toBool();
        Q_EMIT batteryChanged();
    });
}

void Device::onBatteryRefreshed(bool charging, int charge)
{
    m_hasBattery = true;
    m_charging = charging;
    m_charge = charge;
    Q_EMIT batteryChanged();
}

void Device::requestPairing()
{
    kdc::call(path(), kdc::DeviceIface, QStringLiteral("requestPairing"));
}

void Device::acceptPairing()
{
    kdc::call(path(), kdc::DeviceIface, QStringLiteral("acceptPairing"));
}

void Device::cancelPairing()
{
    kdc::call(path(), kdc::DeviceIface, QStringLiteral("cancelPairing"));
}

void Device::unpair()
{
    kdc::call(path(), kdc::DeviceIface, QStringLiteral("unpair"));
}

void Device::ping()
{
    kdc::call(path() + QStringLiteral("/ping"), QStringLiteral("org.kde.kdeconnect.device.ping"), QStringLiteral("sendPing"));
}

void Device::ring()
{
    kdc::call(path() + QStringLiteral("/findmyphone"), QStringLiteral("org.kde.kdeconnect.device.findmyphone"), QStringLiteral("ring"));
}

void Device::browseFiles()
{
    if (m_browsing)
        return;
    m_browsing = true;
    Q_EMIT browsingChanged();

    // startBrowsing() hands a kdeconnect:// URL to KIO, which only resolves in
    // KIO-aware file managers. Mount ourselves and open the local mount point.
    const QString sftpPath = path() + QStringLiteral("/sftp");
    const QString sftpIface = QStringLiteral("org.kde.kdeconnect.device.sftp");
    auto msg = QDBusMessage::createMethodCall(kdc::Service, sftpPath, sftpIface, QStringLiteral("mountAndWait"));
    auto *w = new QDBusPendingCallWatcher(kdc::bus().asyncCall(msg, 60000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [=, this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        auto fail = [this](const QString &error) {
            m_browsing = false;
            Q_EMIT browsingChanged();
            Q_EMIT browseFailed(error);
        };
        if (reply.type() == QDBusMessage::ErrorMessage)
            return fail(reply.errorMessage());
        if (!reply.arguments().value(0).toBool()) {
            kdc::call(sftpPath, sftpIface, QStringLiteral("getMountError"), {}, this, [fail](const QDBusMessage &r) {
                const QString error = r.arguments().value(0).toString();
                fail(error.isEmpty() ? tr("Could not mount the phone's storage") : error);
            });
            return;
        }
        // Prefer the phone's main storage over the bare mount root.
        kdc::call(sftpPath, sftpIface, QStringLiteral("getDirectories"), {}, this, [this, sftpPath, sftpIface](const QDBusMessage &r) {
            const QVariantMap dirs = qdbus_cast<QVariantMap>(r.arguments().value(0));
            auto open = [this](const QString &dir) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
                m_browsing = false;
                Q_EMIT browsingChanged();
            };
            for (auto it = dirs.cbegin(); it != dirs.cend(); ++it) {
                if (QFileInfo(it.key()).isDir())
                    return open(it.key());
            }
            kdc::call(sftpPath, sftpIface, QStringLiteral("mountPoint"), {}, this, [open](const QDBusMessage &m) { open(m.arguments().value(0).toString()); });
        });
    });
}
