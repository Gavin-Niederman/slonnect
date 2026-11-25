#include "settings.h"
#include "devicemanager.h"
#include "kdeconnect.h"

#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

static const QString ConfigIface = QStringLiteral("org.kde.kdeconnect.config");

// --- this computer -------------------------------------------------------------

HostSettings::HostSettings(DeviceManager *devices, QObject *parent)
    : QObject(parent)
{
    auto bus = kdc::bus();
    bus.connect(kdc::Service, kdc::DaemonPath, kdc::DaemonIface, QStringLiteral("announcedNameChanged"), this, SLOT(onNameChanged(QString)));
    bus.connect(kdc::Service, kdc::DaemonPath, kdc::DaemonIface, QStringLiteral("linkProvidersChanged"), this, SLOT(onBackendsChanged(QStringList)));
    bus.connect(kdc::Service, kdc::DaemonPath, kdc::DaemonIface, QStringLiteral("customDevicesChanged"), this, SLOT(onCustomDevicesChanged(QStringList)));
    connect(devices, &DeviceManager::daemonRunningChanged, this, &HostSettings::reload);
    reload();
}

void HostSettings::reload()
{
    kdc::call(kdc::DaemonPath, kdc::DaemonIface, QStringLiteral("announcedName"), {}, this,
              [this](const QDBusMessage &r) { onNameChanged(r.arguments().value(0).toString()); });
    kdc::call(kdc::DaemonPath, kdc::DaemonIface, QStringLiteral("selfId"), {}, this, [this](const QDBusMessage &r) {
        m_id = r.arguments().value(0).toString();
        Q_EMIT changed();
    });
    kdc::call(kdc::DaemonPath, kdc::DaemonIface, QStringLiteral("linkProviders"), {}, this,
              [this](const QDBusMessage &r) { onBackendsChanged(kdc::unwrap(r.arguments().value(0)).toStringList()); });
    kdc::call(kdc::DaemonPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"), {kdc::DaemonIface, QStringLiteral("customDevices")}, this,
              [this](const QDBusMessage &r) { onCustomDevicesChanged(kdc::unwrap(r.arguments().value(0)).toStringList()); });
}

void HostSettings::onNameChanged(const QString &name)
{
    m_name = name;
    Q_EMIT changed();
}

void HostSettings::onBackendsChanged(const QStringList &providers)
{
    // Each entry is "Display name|id|enabled" (or "disabled").
    QVariantList list;
    for (const QString &p : providers) {
        const QStringList parts = p.split(QLatin1Char('|'));
        if (parts.size() < 3)
            continue;
        list << QVariantMap{{QStringLiteral("name"), parts[0]}, {QStringLiteral("id"), parts[1]}, {QStringLiteral("enabled"), parts[2] == QLatin1String("enabled")}};
    }
    m_backends = list;
    Q_EMIT changed();
}

void HostSettings::onCustomDevicesChanged(const QStringList &devices)
{
    m_customDevices = devices;
    Q_EMIT changed();
}

void HostSettings::setName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == m_name)
        return;
    kdc::call(kdc::DaemonPath, kdc::DaemonIface, QStringLiteral("setAnnouncedName"), {trimmed});
}

void HostSettings::setBackendEnabled(const QString &id, bool enabled)
{
    kdc::call(kdc::DaemonPath, kdc::DaemonIface, QStringLiteral("setLinkProviderState"), {id, enabled}, this, [this](const QDBusMessage &) { reload(); });
}

bool HostSettings::addCustomDevice(const QString &address)
{
    const QString a = address.trimmed();
    // kdeconnectd takes an IP address or a host name it can resolve.
    static const QRegularExpression hostName(QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$"));
    if (a.isEmpty() || m_customDevices.contains(a) || (QHostAddress(a).isNull() && !hostName.match(a).hasMatch()))
        return false;
    setCustomDevices(m_customDevices + QStringList{a});
    return true;
}

void HostSettings::removeCustomDevice(const QString &address)
{
    QStringList list = m_customDevices;
    if (list.removeAll(address))
        setCustomDevices(list);
}

void HostSettings::setCustomDevices(const QStringList &devices)
{
    m_customDevices = devices;
    Q_EMIT changed();
    kdc::setProperty(kdc::DaemonPath, kdc::DaemonIface, QStringLiteral("customDevices"), devices);
}

// --- per-device plugins --------------------------------------------------------

PluginSettings::PluginSettings(DeviceManager *devices, QObject *parent)
    : QAbstractListModel(parent)
    , m_devices(devices)
{
    connect(devices, &DeviceManager::currentChanged, this, &PluginSettings::rebind);
    rebind();
}

QString PluginSettings::deviceId() const
{
    return m_device ? m_device->id() : QString();
}

QString PluginSettings::configPath(const QString &deviceId, const QString &plugin)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kdeconnect/") + deviceId + QLatin1Char('/') + plugin
        + QStringLiteral("/config");
}

void PluginSettings::rebind()
{
    if (m_device)
        disconnect(m_device, nullptr, this, nullptr);
    auto bus = kdc::bus();
    for (const QString &path : std::as_const(m_watched))
        bus.disconnect(QString(), path, ConfigIface, QStringLiteral("configChanged"), this, SLOT(onConfigChanged(QDBusMessage)));
    m_watched.clear();

    m_device = m_devices->current();
    if (m_device) {
        connect(m_device, &Device::pluginsChanged, this, &PluginSettings::reload);
        connect(m_device, &Device::changed, this, &PluginSettings::reload);
    }
    Q_EMIT deviceChanged();
    reload();
}

void PluginSettings::reload()
{
    const QStringList supported = m_device ? m_device->supportedPlugins() : QStringList();
    const QStringList loaded = m_device ? m_device->loadedPlugins() : QStringList();

    // Listen for setting changes from other UIs (and the daemon) on every plugin.
    auto bus = kdc::bus();
    for (const QString &plugin : supported) {
        const QString path = QStringLiteral("/kdeconnect/") + m_device->id() + QLatin1Char('/') + plugin;
        if (!m_watched.contains(path)) {
            bus.connect(QString(), path, ConfigIface, QStringLiteral("configChanged"), this, SLOT(onConfigChanged(QDBusMessage)));
            m_watched << path;
        }
    }

    QStringList current;
    for (const Entry &e : std::as_const(m_entries))
        current << e.id;
    if (current != supported) {
        beginResetModel();
        m_entries.clear();
        for (const QString &id : supported)
            m_entries.append({id, loaded.contains(id), loaded.contains(id)});
        endResetModel();
    }

    // Enabled-but-unloaded is possible (the other side doesn't support it), so ask.
    for (int i = 0; i < m_entries.size(); ++i) {
        Entry &e = m_entries[i];
        if (e.loaded != loaded.contains(e.id)) {
            e.loaded = !e.loaded;
            Q_EMIT dataChanged(index(i), index(i), {LoadedRole});
        }
        const QString id = e.id;
        kdc::call(kdc::devicePath(m_device->id()), kdc::DeviceIface, QStringLiteral("isPluginEnabled"), {id}, this, [this, id](const QDBusMessage &r) {
            const bool enabled = r.arguments().value(0).toBool();
            for (int row = 0; row < m_entries.size(); ++row) {
                if (m_entries[row].id == id && m_entries[row].enabled != enabled) {
                    m_entries[row].enabled = enabled;
                    Q_EMIT dataChanged(index(row), index(row), {EnabledRole});
                }
            }
        });
    }
}

int PluginSettings::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant PluginSettings::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case IdRole:
    case Qt::DisplayRole:
        return e.id;
    case EnabledRole:
        return e.enabled;
    case LoadedRole:
        return e.loaded;
    }
    return {};
}

QHash<int, QByteArray> PluginSettings::roleNames() const
{
    return {{IdRole, "pluginId"}, {EnabledRole, "enabled"}, {LoadedRole, "loaded"}};
}

void PluginSettings::setEnabled(const QString &plugin, bool enabled)
{
    if (!m_device)
        return;
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries[row].id == plugin) {
            m_entries[row].enabled = enabled;
            Q_EMIT dataChanged(index(row), index(row), {EnabledRole});
        }
    }
    kdc::call(kdc::devicePath(m_device->id()), kdc::DeviceIface, QStringLiteral("setPluginEnabled"), {plugin, enabled});
}

QVariant PluginSettings::value(const QString &plugin, const QString &key, const QVariant &fallback) const
{
    if (!m_device)
        return fallback;
    QSettings config(configPath(m_device->id(), plugin), QSettings::IniFormat);
    const QVariant v = config.value(key, fallback);
    // QSettings hands back strings for values another process wrote; match the fallback's type.
    if (fallback.isValid() && v.metaType() != fallback.metaType()) {
        if (fallback.metaType() == QMetaType::fromType<bool>())
            return v.toString() == QLatin1String("true") || v.toInt() != 0;
        QVariant converted = v;
        if (converted.convert(fallback.metaType()))
            return converted;
    }
    return v;
}

void PluginSettings::setValue(const QString &plugin, const QString &key, const QVariant &value)
{
    if (!m_device)
        return;
    const QString path = configPath(m_device->id(), plugin);
    QDir().mkpath(QFileInfo(path).absolutePath());
    {
        QSettings config(path, QSettings::IniFormat);
        config.setValue(key, value);
        config.sync();
    }
    announce(plugin);
}

void PluginSettings::reset(const QString &plugin, const QString &key)
{
    if (!m_device)
        return;
    {
        QSettings config(configPath(m_device->id(), plugin), QSettings::IniFormat);
        config.remove(key);
        config.sync();
    }
    announce(plugin);
}

void PluginSettings::announce(const QString &plugin)
{
    // kdeconnectd's plugins reread their config on this signal.
    const auto signal = QDBusMessage::createSignal(QStringLiteral("/kdeconnect/") + m_device->id() + QLatin1Char('/') + plugin, ConfigIface,
                                                   QStringLiteral("configChanged"));
    kdc::bus().send(signal);
}

void PluginSettings::onConfigChanged(const QDBusMessage &msg)
{
    // The signal's object path ends in the plugin id.
    Q_EMIT valuesChanged(msg.path().section(QLatin1Char('/'), -1));
}
