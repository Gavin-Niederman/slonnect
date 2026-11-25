#include "messagetypes.h"

#include <QDBusMetaType>
#include <QDBusVariant>

Q_LOGGING_CATEGORY(lcSms, "slonnect.sms")

QStringList ConversationMessage::addressList() const
{
    QStringList list;
    list.reserve(addresses.size());
    for (const auto &a : addresses)
        list << a.address;
    return list;
}

QDBusArgument &operator<<(QDBusArgument &arg, const ConversationAddress &a)
{
    arg.beginStructure();
    arg << a.address;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, ConversationAddress &a)
{
    arg.beginStructure();
    arg >> a.address;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const MessageAttachment &a)
{
    arg.beginStructure();
    arg << a.partID << a.mimeType << a.base64Thumbnail << a.uniqueIdentifier;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, MessageAttachment &a)
{
    arg.beginStructure();
    arg >> a.partID >> a.mimeType >> a.base64Thumbnail >> a.uniqueIdentifier;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const ConversationMessage &m)
{
    arg.beginStructure();
    arg << m.eventField << m.body << m.addresses << m.date << m.type << m.read << m.threadID << m.uID << m.subID << m.attachments;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, ConversationMessage &m)
{
    arg.beginStructure();
    arg >> m.eventField >> m.body >> m.addresses >> m.date >> m.type >> m.read >> m.threadID >> m.uID >> m.subID >> m.attachments;
    arg.endStructure();
    return arg;
}

void registerMessageTypes()
{
    qDBusRegisterMetaType<ConversationAddress>();
    qDBusRegisterMetaType<QList<ConversationAddress>>();
    qDBusRegisterMetaType<MessageAttachment>();
    qDBusRegisterMetaType<QList<MessageAttachment>>();
    qDBusRegisterMetaType<ConversationMessage>();
}

bool toMessage(const QVariant &v, ConversationMessage &out)
{
    if (v.metaType() == QMetaType::fromType<QDBusVariant>())
        return toMessage(v.value<QDBusVariant>().variant(), out);
    if (v.metaType() == QMetaType::fromType<QDBusArgument>()) {
        v.value<QDBusArgument>() >> out;
        return true;
    }
    if (v.metaType() == QMetaType::fromType<ConversationMessage>()) {
        out = v.value<ConversationMessage>();
        return true;
    }
    return false;
}
