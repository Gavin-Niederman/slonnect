// Backend-neutral messaging types. KDE Connect's conversations and Google
// Messages for Web both convert into these; the models only ever see them.
#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariantList>

struct ConversationMessage;

struct SmsAttachment {
    QString id;            // unique per attachment within its backend
    QString mimeType;
    QString name;
    qint64 size = 0;
    QByteArray thumbnail;  // inline thumbnail bytes (KDE Connect sends these)
    bool remoteThumbnail = false; // a thumbnail can be fetched (Google Messages)
    qint64 kdePartId = -1; // KDE Connect's part ID, needed to download

    bool isVCard() const;
    QString kind() const; // image | video | audio | vcard | file
};

struct SmsMessage {
    QString id;
    QString threadId;
    QString tmpId;     // our ID for a message we sent, echoed back (Google Messages)
    QString body;
    qint64 date = 0;   // ms since epoch
    bool outgoing = false;
    QString status;    // pending | sent | delivered | read | failed | received
    QString error;
    bool read = true;
    QString sender;     // incoming: the sender's address
    QString senderName; // incoming: a name from the backend, if it knows one
    QStringList addresses; // the thread's participants (for new threads)
    bool multi = false;
    QList<SmsAttachment> attachments;
    QString replyTo;        // message ID this one answers
    QVariantList reactions; // [{emoji, count, mine}]
};

struct SmsThread {
    QString id;
    QString title;          // the backend's own name for it (group name, contact name)
    QStringList addresses;  // everyone but us
    QStringList participantIds; // backend IDs, parallel to `addresses` where known
    QString snippet;
    QString attachmentMime; // set when the latest message is media without text
    qint64 date = 0;
    bool read = true;
    bool outgoing = false;
    bool multi = false;
    bool rcs = false;
    QString avatar;         // image URL from the backend, if any
};

Q_DECLARE_METATYPE(SmsAttachment)
Q_DECLARE_METATYPE(SmsMessage)
Q_DECLARE_METATYPE(SmsThread)

// KDE Connect wire message -> neutral types.
SmsMessage smsFromKde(const ConversationMessage &m);
SmsThread threadFromKde(const ConversationMessage &m);
