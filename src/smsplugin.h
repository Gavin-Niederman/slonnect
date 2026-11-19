#pragma once

#include "conversationsmodel.h"
#include "deviceplugin.h"
#include "messagesmodel.h"

#include <QDBusVariant>
#include <QTimer>

class ContactStore;

// SMS / MMS / RCS conversations (kdeconnect_sms + the daemon's conversations cache).
class SmsPlugin : public DevicePlugin
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.sms")
    Q_PROPERTY(ConversationsFilter *conversations READ conversations CONSTANT)
    Q_PROPERTY(MessagesModel *messages READ messages CONSTANT)
    Q_PROPERTY(qint64 currentThread READ currentThread NOTIFY currentChanged)
    Q_PROPERTY(QStringList currentAddresses READ currentAddresses NOTIFY currentChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentChanged)
    Q_PROPERTY(bool composing READ composing NOTIFY currentChanged)
    Q_PROPERTY(bool loadingConversations READ loadingConversations NOTIFY loadingChanged)
    Q_PROPERTY(bool loadingMessages READ loadingMessages NOTIFY loadingChanged)
    Q_PROPERTY(bool reachedStart READ reachedStart NOTIFY loadingChanged)

public:
    SmsPlugin(ContactStore *contacts, QObject *parent = nullptr);

    ConversationsFilter *conversations() { return &m_filter; }
    MessagesModel *messages() { return &m_messages; }
    qint64 currentThread() const { return m_currentThread; }
    QStringList currentAddresses() const { return m_currentAddresses; }
    QString currentTitle() const;
    bool composing() const { return m_currentThread < 0 && !m_currentAddresses.isEmpty(); }
    bool loadingConversations() const { return m_loadingConversations; }
    bool loadingMessages() const { return m_loadingMessages; }
    bool reachedStart() const { return m_reachedStart; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openThread(qint64 threadId);
    // Open the thread with exactly these participants, or a blank draft for them.
    Q_INVOKABLE void startConversation(const QStringList &addresses);
    Q_INVOKABLE void close();
    Q_INVOKABLE void loadOlder();
    // `attachments` are local paths or file:// URLs.
    Q_INVOKABLE bool send(const QString &text, const QVariantList &attachments = {});
    Q_INVOKABLE void downloadAttachment(qint64 partId, const QString &uniqueIdentifier);
    Q_INVOKABLE void openAttachment(qint64 partId, const QString &uniqueIdentifier);

    // Fills the models with synthetic conversations (SLONNECT_DEMO=1) so the UI
    // can be exercised without reading anyone's real messages.
    void loadDemo();

Q_SIGNALS:
    void currentChanged();
    void loadingChanged();
    void messageArrived(qint64 threadId, const QString &from, const QString &body);

protected:
    void attached() override;
    void detached() override;

private Q_SLOTS:
    void onConversationCreated(const QDBusVariant &msg);
    void onConversationUpdated(const QDBusVariant &msg);
    void onConversationRemoved(qlonglong threadId);
    void onConversationLoaded(qlonglong threadId, qulonglong count);
    void onAttachmentReceived(const QString &filePath, const QString &fileName);

private:
    void handleMessage(const ConversationMessage &m, bool created);
    void requestPage();
    void setLoadingMessages(bool loading);

    ContactStore *m_contacts;
    ConversationsModel m_model;
    ConversationsFilter m_filter;
    MessagesModel m_messages;
    qint64 m_currentThread = -1;
    QStringList m_currentAddresses;
    bool m_loadingConversations = false;
    bool m_loadingMessages = false;
    bool m_reachedStart = false;
    int m_countAtRequest = 0;
    QTimer m_idleTimer;
    QSet<QString> m_openWhenReady; // attachment uids to open once downloaded
    QList<ConversationMessage> m_demo;
};
