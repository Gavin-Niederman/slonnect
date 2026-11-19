#pragma once

#include "conversationsmodel.h"
#include "gmessagesbackend.h"
#include "kdeconnectbackend.h"
#include "messagesmodel.h"

#include <QTimer>

class ContactStore;

// Messaging: the conversation list, the open thread and sending, on top of one
// backend at a time: KDE Connect (default) or Google Messages for Web.
class SmsPlugin : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.sms")
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(bool canReact READ canReact NOTIFY backendChanged)
    Q_PROPERTY(bool canReply READ canReply NOTIFY backendChanged)
    Q_PROPERTY(ConversationsFilter *conversations READ conversations CONSTANT)
    Q_PROPERTY(MessagesModel *messages READ messages CONSTANT)
    Q_PROPERTY(QString currentThread READ currentThread NOTIFY currentChanged)
    Q_PROPERTY(bool threadOpen READ threadOpen NOTIFY currentChanged)
    Q_PROPERTY(QStringList currentAddresses READ currentAddresses NOTIFY currentChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentChanged)
    Q_PROPERTY(bool currentRcs READ currentRcs NOTIFY currentChanged)
    Q_PROPERTY(QString currentAvatar READ currentAvatar NOTIFY currentChanged)
    Q_PROPERTY(bool composing READ composing NOTIFY currentChanged)
    Q_PROPERTY(bool loadingConversations READ loadingConversations NOTIFY loadingChanged)
    Q_PROPERTY(bool loadingMessages READ loadingMessages NOTIFY loadingChanged)
    Q_PROPERTY(bool reachedStart READ reachedStart NOTIFY loadingChanged)
    // Who is typing in the open thread (names), Google Messages only.
    Q_PROPERTY(QStringList typing READ typing NOTIFY typingChanged)
    // The message being answered: {id, body, sender}, or empty.
    Q_PROPERTY(QVariantMap replyTo READ replyTo NOTIFY replyToChanged)
    // SIMs to send from (Google Messages, dual SIM) and the chosen one ("" = default).
    Q_PROPERTY(QVariantList sims READ sims NOTIFY simsChanged)
    Q_PROPERTY(QString sim READ sim WRITE setSim NOTIFY simsChanged)

public:
    SmsPlugin(ContactStore *contacts, QObject *parent = nullptr);

    // Binds the KDE Connect backend to the selected device.
    void setDevice(const QString &deviceId, const QStringList &loadedPlugins);
    GMessagesBackend *gmessages() { return &m_gm; }

    bool available() const;
    QString backend() const { return m_backendName; }
    void setBackend(const QString &name);
    bool canReact() const { return m_backend->capabilities() & MessagingBackend::Reactions; }
    bool canReply() const { return m_backend->capabilities() & MessagingBackend::Replies; }
    ConversationsFilter *conversations() { return &m_filter; }
    MessagesModel *messages() { return &m_messages; }
    QString currentThread() const { return m_currentThread; }
    bool threadOpen() const { return !m_currentThread.isEmpty() || composing(); }
    QStringList currentAddresses() const { return m_currentAddresses; }
    QString currentTitle() const;
    bool currentRcs() const;
    QString currentAvatar() const;
    bool composing() const { return m_currentThread.isEmpty() && !m_currentAddresses.isEmpty(); }
    bool loadingConversations() const { return m_loadingConversations; }
    bool loadingMessages() const { return m_loadingMessages; }
    bool reachedStart() const { return m_reachedStart; }
    QStringList typing() const;
    QVariantMap replyTo() const { return m_replyTo; }
    QVariantList sims() const;
    QString sim() const { return m_sim; }
    void setSim(const QString &sim);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openThread(const QString &threadId);
    // Open the thread with exactly these participants, or a blank draft for them.
    Q_INVOKABLE void startConversation(const QStringList &addresses);
    Q_INVOKABLE void close();
    Q_INVOKABLE void loadOlder();
    // `attachments` are local paths or file:// URLs.
    Q_INVOKABLE bool send(const QString &text, const QVariantList &attachments = {});
    Q_INVOKABLE void openAttachment(const QString &attachmentId);
    Q_INVOKABLE void react(const QString &messageId, const QString &emoji);
    Q_INVOKABLE void setReplyTo(const QString &messageId);
    Q_INVOKABLE void clearReply();
    // Call while the user types; throttled.
    Q_INVOKABLE void userTyping();

    // Fills the models with synthetic conversations (SLONNECT_DEMO=1) so the UI
    // can be exercised without reading anyone's real messages.
    void loadDemo();

Q_SIGNALS:
    void availableChanged();
    void backendChanged();
    void currentChanged();
    void loadingChanged();
    void typingChanged();
    void replyToChanged();
    void simsChanged();
    void sendFailed(const QString &error);
    void messageArrived(const QString &threadId, const QString &from, const QString &body);

private:
    void bind(MessagingBackend *backend);
    void onThread(const SmsThread &t);
    void onMessage(const SmsMessage &m, bool live);
    void onPageLoaded(const QString &threadId, bool reachedStart);
    void onAttachment(const QString &attachmentId, bool thumbnail, const QString &path);
    void onTyping(const QString &threadId, const QString &address, bool typing);
    void setLoadingMessages(bool loading);
    void requestPage();
    void markOpenThreadRead();

    ContactStore *m_contacts;
    KdeConnectBackend m_kde;
    GMessagesBackend m_gm;
    MessagingBackend *m_backend = nullptr;
    QString m_backendName;
    ConversationsModel m_model;
    ConversationsFilter m_filter;
    MessagesModel m_messages;
    QString m_currentThread;
    QStringList m_currentAddresses;
    bool m_loadingConversations = false;
    bool m_loadingMessages = false;
    bool m_reachedStart = false;
    QSet<QString> m_openWhenReady; // attachments to open once downloaded
    QHash<QString, QTimer *> m_typing; // address -> expiry, for the open thread
    QVariantMap m_replyTo;
    QString m_sim;
    qint64 m_lastTypingSent = 0;
    bool m_demo = false;
    QList<SmsMessage> m_demoMessages;
};
