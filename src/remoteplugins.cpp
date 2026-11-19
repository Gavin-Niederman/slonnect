#include "remoteplugins.h"
#include "kdeconnect.h"

#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <algorithm>
#include <cmath>

static QString deviceIface(const char *plugin)
{
    return QStringLiteral("org.kde.kdeconnect.device.") + QLatin1String(plugin);
}

// --- remote control ----------------------------------------------------------

RemoteControlPlugin::RemoteControlPlugin(QObject *parent)
    : DevicePlugin(QStringLiteral("kdeconnect_remotecontrol"), QStringLiteral("/remotecontrol"), deviceIface("remotecontrol"), parent)
{
    m_flush.setSingleShot(true);
    m_flush.setInterval(16);
    connect(&m_flush, &QTimer::timeout, this, &RemoteControlPlugin::flush);
}

void RemoteControlPlugin::detached()
{
    m_flush.stop();
    m_pending = {};
}

void RemoteControlPlugin::move(qreal dx, qreal dy)
{
    if (!available())
        return;
    m_pending += QPointF(dx, dy);
    if (!m_flush.isActive())
        m_flush.start();
}

void RemoteControlPlugin::flush()
{
    const QPoint delta(int(std::lround(m_pending.x())), int(std::lround(m_pending.y())));
    // Keep the sub-pixel remainder so slow, precise moves still add up.
    m_pending -= QPointF(delta);
    if (!delta.isNull())
        invoke(QStringLiteral("moveCursor"), {QVariant::fromValue(delta)});
}

void RemoteControlPlugin::command(const QVariantMap &packet)
{
    if (m_flush.isActive()) {
        m_flush.stop();
        flush(); // a click lands where the pointer was last moved
    }
    invoke(QStringLiteral("sendCommand"), {packet});
}

void RemoteControlPlugin::click(const QString &button)
{
    static const QHash<QString, QString> keys{{QStringLiteral("left"), QStringLiteral("singleclick")},
                                              {QStringLiteral("right"), QStringLiteral("rightclick")},
                                              {QStringLiteral("middle"), QStringLiteral("middleclick")},
                                              {QStringLiteral("double"), QStringLiteral("doubleclick")}};
    const QString key = keys.value(button);
    if (!key.isEmpty())
        command({{key, true}});
}

void RemoteControlPlugin::hold()
{
    command({{QStringLiteral("singlehold"), true}});
}

void RemoteControlPlugin::release()
{
    command({{QStringLiteral("singlerelease"), true}});
}

void RemoteControlPlugin::scroll(qreal dx, qreal dy)
{
    command({{QStringLiteral("scroll"), true}, {QStringLiteral("dx"), dx}, {QStringLiteral("dy"), dy}});
}

// --- remote keyboard ---------------------------------------------------------

RemoteKeyboardPlugin::RemoteKeyboardPlugin(QObject *parent)
    : DevicePlugin(QStringLiteral("kdeconnect_remotekeyboard"), QStringLiteral("/remotekeyboard"), deviceIface("remotekeyboard"), parent)
{
}

void RemoteKeyboardPlugin::attached()
{
    subscribe(QStringLiteral("remoteStateChanged"), SLOT(onRemoteStateChanged(bool)));
    kdc::getAll(path(), iface(), this, [this](const QVariantMap &p) { onRemoteStateChanged(p.value(QStringLiteral("remoteState")).toBool()); });
}

void RemoteKeyboardPlugin::detached()
{
    onRemoteStateChanged(false);
}

void RemoteKeyboardPlugin::onRemoteStateChanged(bool state)
{
    if (state != m_remoteState) {
        m_remoteState = state;
        Q_EMIT remoteStateChanged();
    }
}

void RemoteKeyboardPlugin::sendKeyEvent(int key, int modifiers, const QString &text)
{
    // The daemon maps Qt keys to its special-key codes and sends the rest as text.
    invoke(QStringLiteral("sendQKeyEvent"),
           {QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("modifiers"), modifiers}, {QStringLiteral("text"), text}}});
}

void RemoteKeyboardPlugin::sendText(const QString &text)
{
    if (!text.isEmpty())
        invoke(QStringLiteral("sendKeyPress"), {text});
}

void RemoteKeyboardPlugin::sendSpecialKey(int code)
{
    invoke(QStringLiteral("sendKeyPress"), {QString(), code});
}

// --- remote commands ---------------------------------------------------------

RemoteCommandsPlugin::RemoteCommandsPlugin(QObject *parent)
    : DevicePlugin(QStringLiteral("kdeconnect_remotecommands"), QStringLiteral("/remotecommands"), deviceIface("remotecommands"), parent)
{
}

void RemoteCommandsPlugin::attached()
{
    subscribe(QStringLiteral("commandsChanged"), SLOT(onCommandsChanged(QByteArray)));
    kdc::getAll(path(), iface(), this, [this](const QVariantMap &p) {
        m_canAddCommand = p.value(QStringLiteral("canAddCommand")).toBool();
        onCommandsChanged(p.value(QStringLiteral("commands")).toByteArray());
    });
}

void RemoteCommandsPlugin::detached()
{
    m_commands.clear();
    m_canAddCommand = false;
    Q_EMIT commandsChanged();
}

void RemoteCommandsPlugin::onCommandsChanged(const QByteArray &json)
{
    const QJsonObject obj = QJsonDocument::fromJson(json).object();
    QVariantList list;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QJsonObject c = it.value().toObject();
        list << QVariantMap{{QStringLiteral("key"), it.key()},
                            {QStringLiteral("name"), c.value(QStringLiteral("name")).toString()},
                            {QStringLiteral("command"), c.value(QStringLiteral("command")).toString()}};
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().localeAwareCompare(b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    m_commands = list;
    Q_EMIT commandsChanged();
}

void RemoteCommandsPlugin::trigger(const QString &key)
{
    invoke(QStringLiteral("triggerCommand"), {key});
}

void RemoteCommandsPlugin::editCommands()
{
    invoke(QStringLiteral("editCommands"));
}

// --- remote volume -----------------------------------------------------------

RemoteVolumePlugin::RemoteVolumePlugin(QObject *parent)
    : DevicePlugin(QStringLiteral("kdeconnect_remotesystemvolume"), QStringLiteral("/remotesystemvolume"), deviceIface("remotesystemvolume"), parent)
{
}

void RemoteVolumePlugin::attached()
{
    subscribe(QStringLiteral("sinksChanged"), SLOT(reload()));
    subscribe(QStringLiteral("volumeChanged"), SLOT(onVolumeChanged(QString, int)));
    subscribe(QStringLiteral("mutedChanged"), SLOT(onMutedChanged(QString, bool)));
    reload();
}

void RemoteVolumePlugin::detached()
{
    m_sinks.clear();
    Q_EMIT sinksChanged();
}

void RemoteVolumePlugin::reload()
{
    kdc::getAll(path(), iface(), this, [this](const QVariantMap &p) {
        const QJsonArray arr = QJsonDocument::fromJson(p.value(QStringLiteral("sinks")).toByteArray()).array();
        QVariantList list;
        for (const QJsonValue &v : arr) {
            const QJsonObject s = v.toObject();
            list << QVariantMap{{QStringLiteral("name"), s.value(QStringLiteral("name")).toString()},
                                {QStringLiteral("description"), s.value(QStringLiteral("description")).toString()},
                                {QStringLiteral("volume"), s.value(QStringLiteral("volume")).toInt()},
                                {QStringLiteral("maxVolume"), s.value(QStringLiteral("maxVolume")).toInt(100)},
                                {QStringLiteral("muted"), s.value(QStringLiteral("muted")).toBool()}};
        }
        m_sinks = list;
        Q_EMIT sinksChanged();
    });
}

void RemoteVolumePlugin::update(const QString &name, const char *key, const QVariant &value)
{
    for (QVariant &v : m_sinks) {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("name")).toString() != name)
            continue;
        m.insert(QLatin1String(key), value);
        v = m;
        Q_EMIT sinkUpdated(name, m.value(QStringLiteral("volume")).toInt(), m.value(QStringLiteral("muted")).toBool());
        return;
    }
}

void RemoteVolumePlugin::onVolumeChanged(const QString &name, int volume)
{
    update(name, "volume", volume);
}

void RemoteVolumePlugin::onMutedChanged(const QString &name, bool muted)
{
    update(name, "muted", muted);
}

void RemoteVolumePlugin::setVolume(const QString &name, int volume)
{
    invoke(QStringLiteral("sendVolume"), {name, volume});
}

void RemoteVolumePlugin::setMuted(const QString &name, bool muted)
{
    invoke(QStringLiteral("sendMuted"), {name, muted});
}

// --- lock device -------------------------------------------------------------

LockDevicePlugin::LockDevicePlugin(QObject *parent)
    : DevicePlugin(QStringLiteral("kdeconnect_lockdevice"), QStringLiteral("/lockdevice"), deviceIface("lockdevice"), parent)
{
}

void LockDevicePlugin::attached()
{
    subscribe(QStringLiteral("lockedChanged"), SLOT(onLockedChanged(bool)));
    kdc::getAll(path(), iface(), this, [this](const QVariantMap &p) { onLockedChanged(p.value(QStringLiteral("isLocked")).toBool()); });
}

void LockDevicePlugin::onLockedChanged(bool locked)
{
    if (locked != m_locked) {
        m_locked = locked;
        Q_EMIT lockedChanged();
    }
}

void LockDevicePlugin::setLocked(bool locked)
{
    invoke(QStringLiteral("setLocked"), {locked});
}

// --- virtual monitor ---------------------------------------------------------

VirtualMonitorPlugin::VirtualMonitorPlugin(QObject *parent)
    : DevicePlugin(QStringLiteral("kdeconnect_virtualmonitor"), QStringLiteral("/virtualmonitor"), deviceIface("virtualmonitor"), parent)
{
}

void VirtualMonitorPlugin::attached()
{
    subscribe(QStringLiteral("activeChanged"), SLOT(refresh()));
    refresh();
}

void VirtualMonitorPlugin::detached()
{
    if (m_active) {
        m_active = false;
        Q_EMIT activeChanged();
    }
}

void VirtualMonitorPlugin::refresh()
{
    kdc::getAll(path(), iface(), this, [this](const QVariantMap &p) {
        const bool active = p.value(QStringLiteral("active")).toBool();
        if (active != m_active) {
            m_active = active;
            Q_EMIT activeChanged();
        }
    });
}

void VirtualMonitorPlugin::start()
{
    if (!available())
        return;
    kdc::call(path(), iface(), QStringLiteral("requestVirtualMonitor"), {}, this, [this](const QDBusMessage &reply) {
        if (reply.arguments().value(0).toBool())
            return;
        kdc::getAll(path(), iface(), this, [this](const QVariantMap &p) {
            const QString error = p.value(QStringLiteral("lastError")).toString();
            Q_EMIT failed(error.isEmpty() ? tr("The virtual display couldn't be started") : error);
        });
    });
}

void VirtualMonitorPlugin::stop()
{
    invoke(QStringLiteral("stop"));
}
