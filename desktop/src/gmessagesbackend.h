#pragma once

#include "messagingbackend.h"

#include <QDBusMessage>
#include <QHash>
#include <QSet>
#include <QVariantList>
#include <functional>
#include <QtQml/qqmlregistration.h>

class QDBusServiceWatcher;

// Google Messages for Web, through the slonnect-gm helper (helpers/gmessages) on
// the session bus. Also the QML-facing login state for Settings.
class GMessagesBackend : public MessagingBackend
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.gmessages")
    Q_PROPERTY(bool helperRunning READ helperRunning NOTIFY statusChanged)
    // logged_out | pairing | connecting | ready | phone_offline | inactive | needs_relogin
    Q_PROPERTY(QString state READ state NOTIFY statusChanged)
    Q_PROPERTY(QString account READ account NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    Q_PROPERTY(QString secrets READ secrets NOTIFY statusChanged)
    Q_PROPERTY(QVariantList sims READ sims NOTIFY statusChanged)
    Q_PROPERTY(QString emoji READ emoji NOTIFY pairingChanged)
    Q_PROPERTY(QString loginError READ loginError NOTIFY pairingChanged)
    Q_PROPERTY(bool autoReactivate READ autoReactivate WRITE setAutoReactivate NOTIFY autoReactivateChanged)

public:
    static inline const QString Service = QStringLiteral("dev.slonnect.GMessages");
    static inline const QString Path = QStringLiteral("/dev/slonnect/GMessages");
    static inline const QString Iface = QStringLiteral("dev.slonnect.GMessages1");
    static constexpr uint ProtocolVersion = 1;

    explicit GMessagesBackend(QObject *parent = nullptr);

    bool helperRunning() const { return m_running; }
    QString state() const { return m_state; }
    QString account() const { return m_account; }
    QString error() const { return m_error; }
    QString secrets() const { return m_secrets; }
    QVariantList sims() const { return m_sims; }
    QString emoji() const { return m_emoji; }
    QString loginError() const { return m_loginError; }
    bool autoReactivate() const { return m_autoReactivate; }
    void setAutoReactivate(bool on);

    // Start the helper through D-Bus activation (or its systemd unit) if it isn't running.
    Q_INVOKABLE void ensureRunning();
    // `pasted`: a cURL command, a Cookie header or a JSON object of cookies.
    Q_INVOKABLE void startLogin(const QString &pasted);
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void takeOver();

    bool ready() const override { return m_running && m_state == QLatin1String("ready"); }
    int capabilities() const override { return Reactions | Replies | Typing | ReadReceipts | SimChoice; }
    void refresh() override;
    Q_INVOKABLE void loadContacts();
    void openThread(const QString &threadId) override;
    void loadOlder(const QString &threadId) override;
    void closeThread() override { m_thread.clear(); }
    void send(const QString &threadId, const QStringList &addresses, const QString &body, const QStringList &files, const QString &tmpId,
              const QString &replyTo, const QString &sim) override;
    void fetchAttachment(const SmsAttachment &attachment, bool thumbnail) override;
    void markRead(const QString &threadId, const QString &messageId) override;
    void react(const QString &messageId, const QString &emoji, bool add) override;
    void setTyping(const QString &threadId) override;

Q_SIGNALS:
    void statusChanged();
    void pairingChanged();
    void autoReactivateChanged();
    void loginFinished(bool ok, const QString &error);
    // [{name, number, participantId}] from the phone's contacts, once connected.
    void contactsLoaded(const QVariantList &contacts);

private Q_SLOTS:
    void onStatusChanged(const QString &json);
    void onPairingEmoji(const QString &emoji, const QString &svg);
    void onPairingFinished(bool ok, const QString &error);
    void onConversationChanged(const QString &json);
    void onConversationRemoved(const QString &id);
    void onMessageChanged(const QString &json, bool live);
    void onTyping(const QString &conversationId, const QString &number, bool typing);
    void onSendFailed(const QString &tmpId, const QString &conversationId, const QString &error);

private:
    using Reply = std::function<void(const QDBusMessage &)>;
    // Async call to the helper; `onError` gets the D-Bus error text.
    void call(const QString &method, const QVariantList &args, Reply onReply = {}, std::function<void(const QString &)> onError = {}, int timeoutMs = -1);
    void setRunning(bool running);
    void fetchStatus();
    void fetchPage(const QString &threadId, const QString &cursor);
    SmsThread toThread(const QVariantMap &c);
    SmsMessage toMessage(const QVariantMap &m) const;
    void wantAvatar(const QString &participantId);

    QDBusServiceWatcher *m_watcher = nullptr;
    bool m_running = false;
    QString m_state = QStringLiteral("logged_out");
    QString m_account, m_error, m_secrets, m_emoji, m_loginError;
    QVariantList m_sims;
    bool m_autoReactivate = false;

    QString m_thread;
    QHash<QString, QString> m_cursors;            // thread -> paging cursor
    QHash<QString, QVariantMap> m_conversations;  // id -> latest conversation JSON
    QHash<QString, QString> m_avatars;            // participant ID -> file URL ("" = none)
    QSet<QString> m_avatarRequests;
};
