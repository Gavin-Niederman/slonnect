#pragma once

#include "smstypes.h"

#include <QObject>

// Where conversations come from: KDE Connect's sms plugin, or Google Messages for
// Web through the slonnect-gm helper. SmsPlugin drives one at a time; IDs from
// different backends never mix.
class MessagingBackend : public QObject
{
    Q_OBJECT

public:
    enum Capability {
        Reactions = 0x1,
        Replies = 0x2,
        Typing = 0x4,
        ReadReceipts = 0x8,
        SimChoice = 0x10,
    };

    using QObject::QObject;

    // Live: can list, page and send right now.
    virtual bool ready() const = 0;
    virtual int capabilities() const { return 0; }

    virtual void refresh() = 0;
    // Start paging `threadId` from its newest message; pageLoaded() follows.
    virtual void openThread(const QString &threadId) = 0;
    virtual void loadOlder(const QString &threadId) = 0;
    virtual void closeThread() {}
    // To an existing thread, or (threadId empty) to `addresses`.
    virtual void send(const QString &threadId, const QStringList &addresses, const QString &body, const QStringList &files, const QString &tmpId,
                      const QString &replyTo, const QString &sim) = 0;
    virtual void fetchAttachment(const SmsAttachment &attachment, bool thumbnail) = 0;
    virtual void markRead(const QString &threadId, const QString &messageId) { Q_UNUSED(threadId) Q_UNUSED(messageId) }
    virtual void react(const QString &messageId, const QString &emoji, bool add) { Q_UNUSED(messageId) Q_UNUSED(emoji) Q_UNUSED(add) }
    virtual void setTyping(const QString &threadId) { Q_UNUSED(threadId) }

Q_SIGNALS:
    void readyChanged();
    void loadingThreadsChanged(bool loading);
    void threadChanged(const SmsThread &thread);
    void threadRemoved(const QString &threadId);
    // `live`: it just arrived (as opposed to history being loaded).
    void messageChanged(const SmsMessage &message, bool live);
    void messageRemoved(const QString &threadId, const QString &messageId);
    void pageLoaded(const QString &threadId, bool reachedStart);
    void attachmentReady(const QString &attachmentId, bool thumbnail, const QString &path);
    void typing(const QString &threadId, const QString &address, bool typing);
    void sendFailed(const QString &tmpId, const QString &error);
};
