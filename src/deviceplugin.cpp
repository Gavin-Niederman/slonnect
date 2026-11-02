#include "deviceplugin.h"
#include "kdeconnect.h"

DevicePlugin::DevicePlugin(const QString &pluginId, const QString &pathSuffix, const QString &iface, QObject *parent)
    : QObject(parent)
    , m_pluginId(pluginId)
    , m_pathSuffix(pathSuffix)
    , m_iface(iface)
{
}

void DevicePlugin::setDevice(const QString &deviceId, const QStringList &loadedPlugins)
{
    const bool available = !deviceId.isEmpty() && loadedPlugins.contains(m_pluginId);
    if (deviceId == m_deviceId && available == m_available)
        return;

    detach();
    m_deviceId = deviceId;
    m_available = available;
    if (m_available)
        attached();
    Q_EMIT availableChanged();
}

void DevicePlugin::detach()
{
    if (!m_available)
        return;
    for (const auto &s : std::as_const(m_subscriptions))
        kdc::bus().disconnect(kdc::Service, s.path, s.iface, s.signal, this, s.slot.constData());
    m_subscriptions.clear();
    m_available = false;
    detached();
}

QString DevicePlugin::path() const
{
    return kdc::devicePath(m_deviceId) + m_pathSuffix;
}

void DevicePlugin::invoke(const QString &method, const QVariantList &args)
{
    if (!m_available)
        return;
    kdc::call(path(), m_iface, method, args, this);
}

void DevicePlugin::subscribe(const QString &signal, const char *slot, const QString &pathOverride, const QString &ifaceOverride)
{
    Subscription s{pathOverride.isEmpty() ? path() : pathOverride, ifaceOverride.isEmpty() ? m_iface : ifaceOverride, signal, QByteArray(slot)};
    kdc::bus().connect(kdc::Service, s.path, s.iface, s.signal, this, slot);
    m_subscriptions << s;
}
