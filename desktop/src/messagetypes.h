// Wire types of org.kde.kdeconnect.device.conversations, signature (isa(s)xiixixa(xsss)).
#pragma once

#include <QDBusArgument>
#include <QList>
#include <QLoggingCategory>
#include <QMetaType>
#include <QString>

struct ConversationAddress {
    QString address;
};

struct MessageAttachment {
    qint64 partID = -1;
    QString mimeType;
    QString base64Thumbnail; // only populated for image/video parts
    QString uniqueIdentifier;
};

struct ConversationMessage {
    enum Type { All = 0, Inbox = 1, Sent = 2, Draft = 3, Outbox = 4, Failed = 5, Queued = 6 };
    enum Event { TextMessage = 0x1, MultiTarget = 0x2 };

    qint32 eventField = 0;
    QString body;
    QList<ConversationAddress> addresses;
    qint64 date = 0;
    qint32 type = 0;
    qint32 read = 0;
    qint64 threadID = 0;
    qint32 uID = 0;
    qint64 subID = -1;
    QList<MessageAttachment> attachments;

    bool isOutgoing() const { return type != Inbox && type != All; }
    bool isMultiTarget() const { return eventField & MultiTarget; }
    QStringList addressList() const;
};

Q_DECLARE_METATYPE(ConversationAddress)
Q_DECLARE_METATYPE(MessageAttachment)
Q_DECLARE_METATYPE(ConversationMessage)

// Send/confirm tracing. Logs only lengths, types and flags, never content or addresses.
Q_DECLARE_LOGGING_CATEGORY(lcSms)

QDBusArgument &operator<<(QDBusArgument &arg, const ConversationAddress &a);
const QDBusArgument &operator>>(const QDBusArgument &arg, ConversationAddress &a);
QDBusArgument &operator<<(QDBusArgument &arg, const MessageAttachment &a);
const QDBusArgument &operator>>(const QDBusArgument &arg, MessageAttachment &a);
QDBusArgument &operator<<(QDBusArgument &arg, const ConversationMessage &m);
const QDBusArgument &operator>>(const QDBusArgument &arg, ConversationMessage &m);

void registerMessageTypes();

// Decode a message from whatever shape QtDBus handed us (QDBusVariant, QDBusArgument, ...).
bool toMessage(const QVariant &v, ConversationMessage &out);
