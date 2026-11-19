// Small plugins that control the other device: pointer, keyboard, its predefined
// commands, its volume, its lock screen and virtual-monitor sessions.
#pragma once

#include "deviceplugin.h"

#include <QPoint>
#include <QTimer>
#include <QVariantList>

// kdeconnect_remotecontrol: move the other device's pointer, click and scroll.
class RemoteControlPlugin : public DevicePlugin
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.remoteControl")

public:
    explicit RemoteControlPlugin(QObject *parent = nullptr);

    // Relative motion; coalesced to one D-Bus call per frame.
    Q_INVOKABLE void move(qreal dx, qreal dy);
    // "left" | "right" | "middle" | "double"
    Q_INVOKABLE void click(const QString &button);
    // Press / release the left button (for dragging).
    Q_INVOKABLE void hold();
    Q_INVOKABLE void release();
    Q_INVOKABLE void scroll(qreal dx, qreal dy);

protected:
    void detached() override;

private:
    void flush();
    void command(const QVariantMap &packet);

    QTimer m_flush;
    QPointF m_pending;
};

// kdeconnect_remotekeyboard: type on the other device. On a phone this needs
// KDE Connect's keyboard to be the active input method (`remoteState`).
class RemoteKeyboardPlugin : public DevicePlugin
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.remoteKeyboard")
    Q_PROPERTY(bool remoteState READ remoteState NOTIFY remoteStateChanged)

public:
    explicit RemoteKeyboardPlugin(QObject *parent = nullptr);
    bool remoteState() const { return m_remoteState; }

    // A Qt key event (key, modifiers, text) as QML's KeyEvent delivers it.
    Q_INVOKABLE void sendKeyEvent(int key, int modifiers, const QString &text);
    // Types a whole string.
    Q_INVOKABLE void sendText(const QString &text);
    // KDE Connect special key code (see SpecialKeysMap in kdeconnect), e.g. 25 = F5.
    Q_INVOKABLE void sendSpecialKey(int code);

Q_SIGNALS:
    void remoteStateChanged();

protected:
    void attached() override;
    void detached() override;

private Q_SLOTS:
    void onRemoteStateChanged(bool state);

private:
    bool m_remoteState = false;
};

// kdeconnect_remotecommands: run commands the other device has defined.
class RemoteCommandsPlugin : public DevicePlugin
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.remoteCommands")
    Q_PROPERTY(QVariantList commands READ commands NOTIFY commandsChanged)
    Q_PROPERTY(bool canAddCommand READ canAddCommand NOTIFY commandsChanged)

public:
    explicit RemoteCommandsPlugin(QObject *parent = nullptr);
    QVariantList commands() const { return m_commands; }
    bool canAddCommand() const { return m_canAddCommand; }

    Q_INVOKABLE void trigger(const QString &key);
    // Asks the other device to open its command editor.
    Q_INVOKABLE void editCommands();

Q_SIGNALS:
    void commandsChanged();

protected:
    void attached() override;
    void detached() override;

private Q_SLOTS:
    void onCommandsChanged(const QByteArray &json);

private:
    QVariantList m_commands;
    bool m_canAddCommand = false;
};

// kdeconnect_remotesystemvolume: the other device's audio outputs.
class RemoteVolumePlugin : public DevicePlugin
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.remoteVolume")
    // [{name, description, volume, maxVolume, muted}]
    Q_PROPERTY(QVariantList sinks READ sinks NOTIFY sinksChanged)

public:
    explicit RemoteVolumePlugin(QObject *parent = nullptr);
    QVariantList sinks() const { return m_sinks; }

    Q_INVOKABLE void setVolume(const QString &name, int volume);
    Q_INVOKABLE void setMuted(const QString &name, bool muted);

Q_SIGNALS:
    void sinksChanged();
    // One sink changed; lets the UI update in place instead of rebuilding.
    void sinkUpdated(const QString &name, int volume, bool muted);

protected:
    void attached() override;
    void detached() override;

private Q_SLOTS:
    void reload();
    void onVolumeChanged(const QString &name, int volume);
    void onMutedChanged(const QString &name, bool muted);

private:
    void update(const QString &name, const char *key, const QVariant &value);
    QVariantList m_sinks;
};

// kdeconnect_lockdevice: lock or unlock the other device's screen.
class LockDevicePlugin : public DevicePlugin
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.lockDevice")
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY lockedChanged)

public:
    explicit LockDevicePlugin(QObject *parent = nullptr);
    bool locked() const { return m_locked; }
    void setLocked(bool locked);

Q_SIGNALS:
    void lockedChanged();

protected:
    void attached() override;

private Q_SLOTS:
    void onLockedChanged(bool locked);

private:
    bool m_locked = false;
};

// kdeconnect_virtualmonitor: use the other device as an extra screen (VNC/RDP).
class VirtualMonitorPlugin : public DevicePlugin
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.virtualMonitor")
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit VirtualMonitorPlugin(QObject *parent = nullptr);
    bool active() const { return m_active; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

Q_SIGNALS:
    void activeChanged();
    void failed(const QString &error);

protected:
    void attached() override;
    void detached() override;

private Q_SLOTS:
    void refresh();

private:
    bool m_active = false;
};
