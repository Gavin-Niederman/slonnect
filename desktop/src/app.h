#pragma once

#include "actionstore.h"
#include "clipboardplugin.h"
#include "completer.h"
#include "contactstore.h"
#include "devicemanager.h"
#include "mediaplugin.h"
#include "notificationsplugin.h"
#include "remoteplugins.h"
#include "screenmirror.h"
#include "settings.h"
#include "shareplugin.h"
#include "smsplugin.h"
#include "slonnectlink.h"

#include <QObject>
#include <QUrl>
#include <memory>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

// Root object exposed to QML as the `App` singleton. Owns the device list and
// one controller per supported KDE Connect plugin, all bound to the current device.
class App : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(DeviceManager *devices READ devices CONSTANT)
    Q_PROPERTY(Device *device READ device NOTIFY deviceChanged)
    Q_PROPERTY(ContactStore *contacts READ contacts CONSTANT)
    Q_PROPERTY(SmsPlugin *sms READ sms CONSTANT)
    Q_PROPERTY(GMessagesBackend *gmessages READ gmessages CONSTANT)
    Q_PROPERTY(SharePlugin *share READ share CONSTANT)
    Q_PROPERTY(ClipboardPlugin *clipboard READ clipboard CONSTANT)
    Q_PROPERTY(MediaPlugin *media READ media CONSTANT)
    Q_PROPERTY(NotificationsPlugin *notifications READ notifications CONSTANT)
    Q_PROPERTY(RemoteControlPlugin *remoteControl READ remoteControl CONSTANT)
    Q_PROPERTY(RemoteKeyboardPlugin *remoteKeyboard READ remoteKeyboard CONSTANT)
    Q_PROPERTY(RemoteCommandsPlugin *remoteCommands READ remoteCommands CONSTANT)
    Q_PROPERTY(RemoteVolumePlugin *remoteVolume READ remoteVolume CONSTANT)
    Q_PROPERTY(LockDevicePlugin *lockDevice READ lockDevice CONSTANT)
    Q_PROPERTY(VirtualMonitorPlugin *virtualMonitor READ virtualMonitor CONSTANT)
    Q_PROPERTY(HostSettings *host READ host CONSTANT)
    Q_PROPERTY(PluginSettings *plugins READ plugins CONSTANT)
    Q_PROPERTY(ScreenMirror *mirror READ mirror CONSTANT)
    Q_PROPERTY(ActionStore *actions READ actions CONSTANT)
    Q_PROPERTY(Completer *completer READ completer CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    static App *instance();
    static App *create(QQmlEngine *, QJSEngine *);
    // Headless runs (slonnect --run-action) skip anything that touches KDE Connect's
    // configuration. Call before instance().
    static void setHeadless(bool headless);

    DeviceManager *devices() { return &m_devices; }
    Device *device() const { return m_devices.current(); }
    ContactStore *contacts() { return &m_contacts; }
    SmsPlugin *sms() { return &m_sms; }
    GMessagesBackend *gmessages() { return m_sms.gmessages(); }
    SharePlugin *share() { return &m_share; }
    ClipboardPlugin *clipboard() { return &m_clipboard; }
    MediaPlugin *media() { return &m_media; }
    NotificationsPlugin *notifications() { return &m_notifications; }
    RemoteControlPlugin *remoteControl() { return &m_remoteControl; }
    RemoteKeyboardPlugin *remoteKeyboard() { return &m_remoteKeyboard; }
    RemoteCommandsPlugin *remoteCommands() { return &m_remoteCommands; }
    RemoteVolumePlugin *remoteVolume() { return &m_remoteVolume; }
    LockDevicePlugin *lockDevice() { return &m_lockDevice; }
    VirtualMonitorPlugin *virtualMonitor() { return &m_virtualMonitor; }
    HostSettings *host() { return &m_host; }
    PluginSettings *plugins() { return &m_plugins; }
    ScreenMirror *mirror() { return &m_mirror; }
    ActionStore *actions() { return &m_actions; }
    SlonnectLink *link() { return m_link.get(); }
    Completer *completer() { return &m_completer; }
    QString version() const;

    // Handle sms:/smsto: links passed on the command line.
    void handleUrl(const QString &url);
    // Command line of a launch (ours, or a second instance's forwarded to us):
    // sms: links plus kdeconnect-app's --device / --plugin-config and
    // kdeconnect-sms's --device / --message.
    void handleArguments(const QStringList &args);

    // Text handed over by `--message`, consumed once by the Messages page.
    Q_INVOKABLE QString takeDraft();
    Q_INVOKABLE QString formatSize(qint64 bytes) const;
    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }

Q_SIGNALS:
    void deviceChanged();
    void showMessages();
    // Bring the window up, optionally on a page (Features key) / plugin's settings.
    void activateRequested(const QString &page, const QString &plugin);

private:
    explicit App(QObject *parent = nullptr);
    void rebind();

    DeviceManager m_devices;
    ContactStore m_contacts;
    SmsPlugin m_sms;
    SharePlugin m_share;
    ClipboardPlugin m_clipboard;
    MediaPlugin m_media;
    NotificationsPlugin m_notifications;
    RemoteControlPlugin m_remoteControl;
    RemoteKeyboardPlugin m_remoteKeyboard;
    RemoteCommandsPlugin m_remoteCommands;
    RemoteVolumePlugin m_remoteVolume;
    LockDevicePlugin m_lockDevice;
    VirtualMonitorPlugin m_virtualMonitor;
    ScreenMirror m_mirror;
    ActionStore m_actions;
    Completer m_completer;
    HostSettings m_host;
    PluginSettings m_plugins;
    // Slonnect protocol: phone-side nodes, actions on the phone (created once m_demo is known).
    std::unique_ptr<SlonnectLink> m_link;
    QPointer<Device> m_boundDevice;
    QString m_pendingUrl;
    QString m_pendingDraft;
    bool m_demo = false;
};
