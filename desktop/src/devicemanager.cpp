#include "devicemanager.h"
#include "kdeconnect.h"

#include <QDBusPendingCallWatcher>

#include <QDBusServiceWatcher>
#include <QSettings>

DeviceManager::DeviceManager(QObject *parent)
    : QAbstractListModel(parent)
{
    m_preferredId = QSettings().value(QStringLiteral("device/current")).toString();

    auto bus = kdc::bus();
    bus.connect(kdc::Service, kdc::DaemonPath, kdc::DaemonIface, QStringLiteral("deviceListChanged"), this, SLOT(reload()));
    bus.connect(kdc::Service, kdc::DaemonPath, kdc::DaemonIface, QStringLiteral("pairingRequestsChanged"), this, SLOT(reload()));

    auto *watcher = new QDBusServiceWatcher(kdc::Service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &DeviceManager::reload);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        setDaemonRunning(false);
        reload();
    });

    reload();
}

void DeviceManager::setDaemonRunning(bool running)
{
    if (running != m_daemonRunning) {
        m_daemonRunning = running;
        Q_EMIT daemonRunningChanged();
    }
}

void DeviceManager::reload()
{
    auto msg = QDBusMessage::createMethodCall(kdc::Service, kdc::DaemonPath, kdc::DaemonIface, QStringLiteral("devices"));
    msg.setArguments({false, false});
    auto *w = new QDBusPendingCallWatcher(kdc::bus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const auto reply = w->reply();
        QStringList ids;
        if (reply.type() == QDBusMessage::ReplyMessage) {
            setDaemonRunning(true);
            ids = kdc::unwrap(reply.arguments().value(0)).toStringList();
        } else {
            setDaemonRunning(false);
        }

        // Remove vanished devices.
        for (int i = m_devices.size() - 1; i >= 0; --i) {
            if (!ids.contains(m_devices[i]->id())) {
                beginRemoveRows({}, i, i);
                m_devices.takeAt(i)->deleteLater();
                endRemoveRows();
            }
        }
        // Add new ones.
        for (const QString &id : std::as_const(ids)) {
            auto it = std::find_if(m_devices.cbegin(), m_devices.cend(), [&](Device *d) { return d->id() == id; });
            if (it != m_devices.cend())
                continue;
            auto *device = new Device(id, this);
            connect(device, &Device::changed, this, [this, device] {
                const int row = m_devices.indexOf(device);
                if (row >= 0)
                    Q_EMIT dataChanged(index(row), index(row));
                pickCurrent();
                updatePairingRequest();
            });
            connect(device, &Device::pairingFailed, this, [this, device](const QString &error) { Q_EMIT pairingFailed(device->name(), error); });
            beginInsertRows({}, m_devices.size(), m_devices.size());
            m_devices << device;
            endInsertRows();
        }
        Q_EMIT countChanged();
        pickCurrent();
        updatePairingRequest();
    });
}

void DeviceManager::updatePairingRequest()
{
    Device *request = nullptr;
    for (Device *d : std::as_const(m_devices)) {
        if (d->pairRequestedByPeer()) {
            request = d;
            break;
        }
    }
    if (request != m_pairingRequest) {
        m_pairingRequest = request;
        Q_EMIT pairingRequestChanged();
    }
}

Device *DeviceManager::find(const QString &id) const
{
    for (Device *d : m_devices)
        if (d->id() == id)
            return d;
    return nullptr;
}

void DeviceManager::pickCurrent()
{
    Device *best = nullptr;
    // An explicit user choice wins as long as the device is still paired.
    for (Device *d : std::as_const(m_devices)) {
        if (d->id() == m_preferredId && d->paired()) {
            best = d;
            break;
        }
    }
    if (!best) {
        for (Device *d : std::as_const(m_devices)) {
            if (d->paired() && (!best || (d->reachable() && !best->reachable())))
                best = d;
        }
    }
    if (best != m_current) {
        m_current = best;
        Q_EMIT currentChanged();
    }
}

void DeviceManager::select(const QString &id)
{
    m_preferredId = id;
    QSettings().setValue(QStringLiteral("device/current"), id);
    pickCurrent();
}

void DeviceManager::refreshDiscovery()
{
    kdc::call(kdc::DaemonPath, kdc::DaemonIface, QStringLiteral("forceOnNetworkChange"));
}

int DeviceManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_devices.size());
}

QVariant DeviceManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_devices.size())
        return {};
    Device *d = m_devices.at(index.row());
    switch (role) {
    case DeviceRole:
        return QVariant::fromValue(d);
    case NameRole:
    case Qt::DisplayRole:
        return d->name();
    case ReachableRole:
        return d->reachable();
    case PairedRole:
        return d->paired();
    case IdRole:
        return d->id();
    }
    return {};
}

QHash<int, QByteArray> DeviceManager::roleNames() const
{
    return {{DeviceRole, "device"}, {NameRole, "name"}, {ReachableRole, "reachable"}, {PairedRole, "paired"}, {IdRole, "deviceId"}};
}
