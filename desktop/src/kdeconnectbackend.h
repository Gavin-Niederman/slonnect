#pragma once

#include "deviceplugin.h"
#include "messagingbackend.h"

#include <QDBusVariant>
#include <QHash>
#include <QTimer>

struct ConversationMessage;

// KDE Connect's sms plugin, through kdeconnectd's conversations cache.
class KdeConnectBackend : public MessagingBackend
{
    Q_OBJECT

public:
    explicit KdeConnectBackend(QObject *parent = nullptr);

    void setDevice(const QString &deviceId, const QStringList &loadedPlugins) { m_plugin.setDevice(deviceId, loadedPlugins); }

    bool ready() const override { return m_plugin.available(); }
    void refresh() override;
    void openThread(const QString &threadId) override;
    void loadOlder(const QString &threadId) override;
    void closeThread() override;
    void send(const QString &threadId, const QStringList &addresses, const QString &body, const QStringList &files, const QString &tmpId,
              const QString &replyTo, const QString &sim) override;
    void fetchAttachment(const SmsAttachment &attachment, bool thumbnail) override;

private:
    // The D-Bus side: a DevicePlugin, so it binds to the selected device.
    class Plugin : public DevicePlugin
    {
    public:
        Plugin(KdeConnectBackend *owner);
        using DevicePlugin::invoke;
        using DevicePlugin::path;
        using DevicePlugin::iface;

    protected:
        void attached() override;
        void detached() override;

    private:
        KdeConnectBackend *m_owner;
    };
    friend class Plugin;

    void attached();
    void detached();
    void requestPage();
    void handle(const ConversationMessage &m, bool created);

private Q_SLOTS:
    void onConversationCreated(const QDBusVariant &msg);
    void onConversationUpdated(const QDBusVariant &msg);
    void onConversationRemoved(qlonglong threadId);
    void onConversationLoaded(qlonglong threadId, qulonglong count);
    void onAttachmentReceived(const QString &filePath, const QString &fileName);

private:
    Plugin m_plugin;
    QString m_thread;
    int m_delivered = 0;    // messages of m_thread seen since it was opened
    int m_countAtRequest = 0;
    bool m_paging = false;
    QTimer m_idleTimer;
    QHash<QString, qint64> m_heads; // thread -> newest message date
};
