#include "smstypes.h"
#include "messagetypes.h"

bool SmsAttachment::isVCard() const
{
    return mimeType.contains(QLatin1String("vcard"), Qt::CaseInsensitive) || mimeType.contains(QLatin1String("x-vcf"), Qt::CaseInsensitive);
}

QString SmsAttachment::kind() const
{
    if (mimeType.startsWith(QLatin1String("image/")))
        return QStringLiteral("image");
    if (mimeType.startsWith(QLatin1String("video/")))
        return QStringLiteral("video");
    if (mimeType.startsWith(QLatin1String("audio/")))
        return QStringLiteral("audio");
    if (isVCard())
        return QStringLiteral("vcard");
    return QStringLiteral("file");
}

SmsMessage smsFromKde(const ConversationMessage &m)
{
    SmsMessage s;
    s.id = QString::number(m.uID);
    s.threadId = QString::number(m.threadID);
    s.body = m.body;
    s.date = m.date;
    s.outgoing = m.isOutgoing();
    s.read = m.read != 0;
    s.addresses = m.addressList();
    s.multi = m.isMultiTarget() || m.addresses.size() > 1;
    if (!s.outgoing && !m.addresses.isEmpty())
        s.sender = m.addresses.first().address;
    switch (m.type) {
    case ConversationMessage::Outbox:
    case ConversationMessage::Queued:
        s.status = QStringLiteral("pending");
        break;
    case ConversationMessage::Failed:
        s.status = QStringLiteral("failed");
        break;
    default:
        s.status = s.outgoing ? QStringLiteral("sent") : QStringLiteral("received");
    }
    for (const MessageAttachment &a : m.attachments) {
        SmsAttachment att;
        att.id = a.uniqueIdentifier;
        att.name = a.uniqueIdentifier;
        att.mimeType = a.mimeType;
        att.kdePartId = a.partID;
        if (!a.base64Thumbnail.isEmpty())
            att.thumbnail = QByteArray::fromBase64(a.base64Thumbnail.toLatin1());
        s.attachments << att;
    }
    return s;
}

SmsThread threadFromKde(const ConversationMessage &m)
{
    SmsThread t;
    t.id = QString::number(m.threadID);
    t.addresses = m.addressList();
    t.snippet = m.body.simplified();
    if (t.snippet.isEmpty() && !m.attachments.isEmpty())
        t.attachmentMime = m.attachments.first().mimeType;
    t.date = m.date;
    t.read = m.read != 0;
    t.outgoing = m.isOutgoing();
    t.multi = m.isMultiTarget() || m.addresses.size() > 1;
    return t;
}
