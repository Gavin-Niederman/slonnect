#include "messagesmodel.h"
#include "contactstore.h"
#include "imagestore.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

static constexpr qint64 GroupGapMs = 5 * 60 * 1000;

MessagesModel::MessagesModel(ContactStore *contacts, QObject *parent)
    : QAbstractListModel(parent)
    , m_contacts(contacts)
{
    connect(contacts, &ContactStore::contactsChanged, this, [this] {
        if (!m_items.isEmpty())
            Q_EMIT dataChanged(index(0), index(int(m_items.size()) - 1), {SenderNameRole, SenderAvatarRole});
    });
}

void MessagesModel::reset(qint64 threadId, bool multi)
{
    beginResetModel();
    m_threadId = threadId;
    m_multi = multi;
    m_items.clear();
    m_known.clear();
    m_pendingCount = 0;
    endResetModel();
}

static bool isVCard(const QString &mime)
{
    return mime.contains(QLatin1String("vcard"), Qt::CaseInsensitive) || mime.contains(QLatin1String("x-vcf"), Qt::CaseInsensitive);
}

bool MessagesModel::add(const ConversationMessage &message)
{
    if (message.threadID != m_threadId)
        return false;
    if (m_known.contains(message.uID)) {
        // Same row again, e.g. outbox -> sent/failed: refresh it in place.
        for (int i = 0; i < m_items.size(); ++i) {
            Item &item = m_items[i];
            if (!item.pending && item.message.uID == message.uID && item.message.type != message.type) {
                qCDebug(lcSms) << "status update: type" << item.message.type << "->" << message.type;
                item.message.type = message.type;
                Q_EMIT dataChanged(index(i), index(i), {StatusRole});
                break;
            }
        }
        return false;
    }
    m_known.insert(message.uID);

    // A confirmed outgoing message replaces our optimistic bubble. Compare whitespace-
    // normalised, since the phone may store line breaks differently than we sent them.
    if (message.isOutgoing() && m_pendingCount > 0) {
        const QString body = message.body.simplified();
        bool matched = false;
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].pending && m_items[i].message.body.simplified() == body) {
                beginRemoveRows({}, i, i);
                m_items.removeAt(i);
                --m_pendingCount;
                endRemoveRows();
                if (i < m_items.size())
                    touchNeighbours(i);
                matched = true;
                break;
            }
        }
        qCDebug(lcSms) << "outgoing confirmation: matched" << matched << "type" << message.type << "len" << message.body.size()
                       << "pendingLeft" << m_pendingCount << "ageMs" << QDateTime::currentMSecsSinceEpoch() - message.date;
    }

    for (const MessageAttachment &a : message.attachments) {
        if (!a.base64Thumbnail.isEmpty() && !ImageStore::instance()->contains(QStringLiteral("att/") + a.uniqueIdentifier))
            ImageStore::instance()->insert(QStringLiteral("att/") + a.uniqueIdentifier, QByteArray::fromBase64(a.base64Thumbnail.toLatin1()));
        // Contact cards are tiny; fetch eagerly so we can render them inline.
        if (isVCard(a.mimeType) && !m_vcards.contains(a.uniqueIdentifier))
            Q_EMIT wantAttachment(a.partID, a.uniqueIdentifier);
    }

    // Newest first: skip past pending items (always newest) and newer messages.
    int pos = 0;
    while (pos < m_items.size() && (m_items[pos].pending || m_items[pos].message.date > message.date))
        ++pos;
    beginInsertRows({}, pos, pos);
    m_items.insert(pos, Item{message, false, {}});
    endInsertRows();
    touchNeighbours(pos);
    return true;
}

void MessagesModel::addPending(const QString &body, const QStringList &attachmentPaths)
{
    ConversationMessage m;
    m.body = body;
    m.date = QDateTime::currentMSecsSinceEpoch();
    m.type = ConversationMessage::Queued;
    m.threadID = m_threadId;
    beginInsertRows({}, 0, 0);
    m_items.prepend(Item{m, true, attachmentPaths});
    ++m_pendingCount;
    endInsertRows();
    touchNeighbours(0);
}

void MessagesModel::attachmentDownloaded(const QString &uniqueIdentifier, const QString &filePath)
{
    m_localFiles.insert(uniqueIdentifier, filePath);
    for (int i = 0; i < m_items.size(); ++i) {
        for (const MessageAttachment &a : std::as_const(m_items[i].message.attachments)) {
            if (a.uniqueIdentifier != uniqueIdentifier)
                continue;
            if (isVCard(a.mimeType)) {
                QFile f(filePath);
                if (f.open(QIODevice::ReadOnly)) {
                    const Contact c = ContactStore::parseVCard(f.readAll(), QStringLiteral("card/") + uniqueIdentifier);
                    m_vcards.insert(uniqueIdentifier,
                                    {{QStringLiteral("name"), c.name},
                                     {QStringLiteral("numbers"), c.numbers},
                                     {QStringLiteral("emails"), c.emails},
                                     {QStringLiteral("photo"), c.photoKey.isEmpty() ? QString() : ImageStore::urlFor(c.photoKey)}});
                }
            }
            Q_EMIT dataChanged(index(i), index(i), {AttachmentsRole});
        }
    }
}

QString MessagesModel::senderOf(const Item &item) const
{
    if (item.message.isOutgoing() || item.pending)
        return QString();
    return item.message.addresses.isEmpty() ? QString() : item.message.addresses.first().address;
}

bool MessagesModel::sameGroup(int a, int b) const
{
    if (a < 0 || b < 0 || a >= m_items.size() || b >= m_items.size())
        return false;
    const Item &x = m_items.at(a);
    const Item &y = m_items.at(b);
    if (x.message.isOutgoing() != y.message.isOutgoing() || senderOf(x) != senderOf(y))
        return false;
    if (std::abs(x.message.date - y.message.date) > GroupGapMs)
        return false;
    return QDateTime::fromMSecsSinceEpoch(x.message.date).date() == QDateTime::fromMSecsSinceEpoch(y.message.date).date();
}

void MessagesModel::touchNeighbours(int row)
{
    const int first = qMax(0, row - 1);
    const int last = qMin(int(m_items.size()) - 1, row + 1);
    if (first <= last)
        Q_EMIT dataChanged(index(first), index(last), {ShowDayRole, GroupStartRole, GroupEndRole});
}

QVariantList MessagesModel::attachmentsFor(const Item &item) const
{
    QVariantList list;
    if (item.pending) {
        for (const QString &path : item.pendingFiles) {
            const QString lower = path.toLower();
            const bool image = lower.endsWith(QLatin1String(".jpg")) || lower.endsWith(QLatin1String(".jpeg")) || lower.endsWith(QLatin1String(".png"))
                || lower.endsWith(QLatin1String(".gif")) || lower.endsWith(QLatin1String(".webp"));
            const bool vcard = lower.endsWith(QLatin1String(".vcf"));
            QVariantMap m{{QStringLiteral("kind"), image ? QStringLiteral("image") : vcard ? QStringLiteral("vcard") : QStringLiteral("file")},
                          {QStringLiteral("thumbnail"), image ? QUrl::fromLocalFile(path).toString() : QString()},
                          {QStringLiteral("localPath"), path},
                          {QStringLiteral("name"), QFileInfo(path).fileName()}};
            if (vcard) {
                QFile f(path);
                if (f.open(QIODevice::ReadOnly)) {
                    const Contact c = ContactStore::parseVCard(f.readAll());
                    m[QStringLiteral("contact")] = QVariantMap{{QStringLiteral("name"), c.name}, {QStringLiteral("numbers"), c.numbers}};
                }
            }
            list << m;
        }
        return list;
    }
    for (const MessageAttachment &a : item.message.attachments) {
        QString kind = QStringLiteral("file");
        if (a.mimeType.startsWith(QLatin1String("image/")))
            kind = QStringLiteral("image");
        else if (a.mimeType.startsWith(QLatin1String("video/")))
            kind = QStringLiteral("video");
        else if (a.mimeType.startsWith(QLatin1String("audio/")))
            kind = QStringLiteral("audio");
        else if (isVCard(a.mimeType))
            kind = QStringLiteral("vcard");
        QVariantMap m{{QStringLiteral("kind"), kind},
                      {QStringLiteral("partId"), a.partID},
                      {QStringLiteral("uid"), a.uniqueIdentifier},
                      {QStringLiteral("mimeType"), a.mimeType},
                      {QStringLiteral("name"), a.uniqueIdentifier},
                      {QStringLiteral("thumbnail"), a.base64Thumbnail.isEmpty() ? QString() : ImageStore::urlFor(QStringLiteral("att/") + a.uniqueIdentifier)},
                      {QStringLiteral("localPath"), m_localFiles.value(a.uniqueIdentifier)}};
        if (kind == QLatin1String("vcard"))
            m[QStringLiteral("contact")] = m_vcards.value(a.uniqueIdentifier);
        list << m;
    }
    return list;
}

int MessagesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant MessagesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const int row = index.row();
    const Item &item = m_items.at(row);
    const ConversationMessage &m = item.message;
    switch (role) {
    case Qt::DisplayRole:
    case BodyRole:
        return m.body;
    case DateRole:
        return m.date;
    case OutgoingRole:
        return item.pending || m.isOutgoing();
    case StatusRole:
        if (item.pending || m.type == ConversationMessage::Outbox || m.type == ConversationMessage::Queued)
            return QStringLiteral("pending");
        if (m.type == ConversationMessage::Failed)
            return QStringLiteral("failed");
        return m.isOutgoing() ? QStringLiteral("sent") : QStringLiteral("received");
    case SenderRole:
        return senderOf(item);
    case SenderNameRole: {
        const QString s = senderOf(item);
        return s.isEmpty() ? QString() : m_contacts->nameFor(s);
    }
    case SenderAvatarRole: {
        const QString s = senderOf(item);
        return s.isEmpty() ? QString() : m_contacts->avatarFor(s);
    }
    case AttachmentsRole:
        return attachmentsFor(item);
    case ShowDayRole: {
        if (row + 1 >= m_items.size())
            return true;
        return QDateTime::fromMSecsSinceEpoch(m.date).date() != QDateTime::fromMSecsSinceEpoch(m_items.at(row + 1).message.date).date();
    }
    case GroupStartRole: // visually above = older = row + 1
        return !sameGroup(row, row + 1);
    case GroupEndRole:
        return !sameGroup(row, row - 1);
    case MultiRole:
        return m_multi;
    }
    return {};
}

QHash<int, QByteArray> MessagesModel::roleNames() const
{
    return {
        {BodyRole, "body"},
        {DateRole, "date"},
        {OutgoingRole, "outgoing"},
        {StatusRole, "status"},
        {SenderRole, "sender"},
        {SenderNameRole, "senderName"},
        {SenderAvatarRole, "senderAvatar"},
        {AttachmentsRole, "attachments"},
        {ShowDayRole, "showDay"},
        {GroupStartRole, "groupStart"},
        {GroupEndRole, "groupEnd"},
        {MultiRole, "multi"},
    };
}
