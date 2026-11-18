#pragma once

#include "actionstore.h"
#include "clipboardplugin.h"
#include "contactstore.h"
#include "devicemanager.h"
#include "mediaplugin.h"
#include "screenmirror.h"
#include "shareplugin.h"
#include "smsplugin.h"

#include <QObject>
#include <QUrl>
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
    Q_PROPERTY(SharePlugin *share READ share CONSTANT)
    Q_PROPERTY(ClipboardPlugin *clipboard READ clipboard CONSTANT)
    Q_PROPERTY(MediaPlugin *media READ media CONSTANT)
    Q_PROPERTY(ScreenMirror *mirror READ mirror CONSTANT)
    Q_PROPERTY(ActionStore *actions READ actions CONSTANT)
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
    SharePlugin *share() { return &m_share; }
    ClipboardPlugin *clipboard() { return &m_clipboard; }
    MediaPlugin *media() { return &m_media; }
    ScreenMirror *mirror() { return &m_mirror; }
    ActionStore *actions() { return &m_actions; }
    QString version() const;

    // Handle sms:/smsto: links passed on the command line.
    void handleUrl(const QString &url);

    Q_INVOKABLE QString formatSize(qint64 bytes) const;
    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }

Q_SIGNALS:
    void deviceChanged();
    void showMessages();

private:
    explicit App(QObject *parent = nullptr);
    void rebind();

    DeviceManager m_devices;
    ContactStore m_contacts;
    SmsPlugin m_sms;
    SharePlugin m_share;
    ClipboardPlugin m_clipboard;
    MediaPlugin m_media;
    ScreenMirror m_mirror;
    ActionStore m_actions;
    QPointer<Device> m_boundDevice;
    QString m_pendingUrl;
    bool m_demo = false;
};
