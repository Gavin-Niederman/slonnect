#include "messagesmodel.h"
#include "contactstore.h"
#include "imagestore.h"
#include "messagetypes.h"

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

void MessagesModel::reset(const QString &threadId, bool multi)
{
    beginResetModel();
    m_threadId = threadId;
    m_multi = multi;
    m_items.clear();
    m_known.clear();
    m_pendingCount = 0;
    endResetModel();
}

int MessagesModel::rowOf(const QString &messageId) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (!m_items[i].pending && m_items[i].message.id == messageId)
            return i;
    return -1;
}

void MessagesModel::requestMedia(const SmsMessage &message)
{
    for (const SmsAttachment &a : message.attachments) {
        if (!a.thumbnail.isEmpty() && !ImageStore::instance()->contains(QStringLiteral("att/") + a.id))
            ImageStore::instance()->insert(QStringLiteral("att/") + a.id, a.thumbnail);
        if (a.remoteThumbnail && !m_thumbFiles.contains(a.id) && !m_thumbRequests.contains(a.id)) {
            m_thumbRequests.insert(a.id);
            Q_EMIT wantAttachment(a, true);
        }
        // Contact cards are tiny; fetch eagerly so we can render them inline.
        if (a.isVCard() && !m_vcards.contains(a.id))
            Q_EMIT wantAttachment(a, false);
    }
}

bool MessagesModel::add(const SmsMessage &message)
{
    if (message.threadId != m_threadId)
        return false;
    if (m_known.contains(message.id)) {
        // Same message again: outbox -> sent, delivered, read, reactions, late media.
        const int i = rowOf(message.id);
        if (i >= 0) {
            Item &item = m_items[i];
            const bool statusChanged = item.message.status != message.status;
            if (statusChanged)
                qCDebug(lcSms) << "status update:" << item.message.status << "->" << message.status;
            const QList<int> roles{StatusRole, ErrorRole, ReactionsRole, AttachmentsRole, BodyRole};
            item.message = message;
            requestMedia(message);
            Q_EMIT dataChanged(index(i), index(i), roles);
        }
        return false;
    }
    m_known.insert(message.id);

    // A confirmed outgoing message replaces our optimistic bubble: by the ID we gave it
    // when the backend echoes that, else by its text (whitespace-normalised, since the
    // phone may store line breaks differently than we sent them).
    if (message.outgoing && m_pendingCount > 0) {
        const QString body = message.body.simplified();
        int match = -1;
        for (int i = 0; i < m_items.size() && match < 0; ++i) {
            const Item &p = m_items[i];
            if (!p.pending)
                continue;
            if (!message.tmpId.isEmpty() && !p.message.tmpId.isEmpty() ? p.message.tmpId == message.tmpId : p.message.body.simplified() == body)
                match = i;
        }
        if (match >= 0) {
            beginRemoveRows({}, match, match);
            m_items.removeAt(match);
            --m_pendingCount;
            endRemoveRows();
            if (match < m_items.size())
                touchNeighbours(match);
        }
        qCDebug(lcSms) << "outgoing confirmation: matched" << (match >= 0) << "status" << message.status << "len" << message.body.size()
                       << "pendingLeft" << m_pendingCount;
    }

    requestMedia(message);

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

void MessagesModel::remove(const QString &messageId)
{
    const int i = rowOf(messageId);
    if (i < 0)
        return;
    beginRemoveRows({}, i, i);
    m_items.removeAt(i);
    endRemoveRows();
    m_known.remove(messageId);
    if (i < m_items.size())
        touchNeighbours(i);
}

void MessagesModel::addPending(const QString &body, const QStringList &attachmentPaths, const QString &tmpId)
{
    SmsMessage m;
    m.body = body;
    m.date = QDateTime::currentMSecsSinceEpoch();
    m.status = QStringLiteral("pending");
    m.outgoing = true;
    m.threadId = m_threadId;
    m.tmpId = tmpId;
    beginInsertRows({}, 0, 0);
    m_items.prepend(Item{m, true, attachmentPaths});
    ++m_pendingCount;
    endInsertRows();
    touchNeighbours(0);
}

void MessagesModel::markFailed(const QString &tmpId, const QString &error)
{
    for (int i = 0; i < m_items.size(); ++i) {
        Item &item = m_items[i];
        if (item.pending && item.message.tmpId == tmpId) {
            item.message.status = QStringLiteral("failed");
            item.message.error = error;
            Q_EMIT dataChanged(index(i), index(i), {StatusRole, ErrorRole});
        }
    }
}

void MessagesModel::attachmentDownloaded(const QString &attachmentId, bool thumbnail, const QString &filePath)
{
    if (thumbnail)
        m_thumbFiles.insert(attachmentId, filePath);
    else
        m_localFiles.insert(attachmentId, filePath);
    for (int i = 0; i < m_items.size(); ++i) {
        for (const SmsAttachment &a : std::as_const(m_items[i].message.attachments)) {
            if (a.id != attachmentId)
                continue;
            if (!thumbnail && a.isVCard()) {
                QFile f(filePath);
                if (f.open(QIODevice::ReadOnly)) {
                    const Contact c = ContactStore::parseVCard(f.readAll(), QStringLiteral("card/") + attachmentId);
                    m_vcards.insert(attachmentId,
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

const SmsAttachment *MessagesModel::findAttachment(const QString &attachmentId) const
{
    for (const Item &item : m_items)
        for (const SmsAttachment &a : item.message.attachments)
            if (a.id == attachmentId)
                return &a;
    return nullptr;
}

const SmsMessage *MessagesModel::findMessage(const QString &messageId) const
{
    const int i = rowOf(messageId);
    return i < 0 ? nullptr : &m_items.at(i).message;
}

QString MessagesModel::newestIncomingId() const
{
    for (const Item &item : m_items)
        if (!item.pending && !item.message.outgoing)
            return item.message.id;
    return QString();
}

QString MessagesModel::senderOf(const Item &item) const
{
    if (item.message.outgoing || item.pending)
        return QString();
    return item.message.sender;
}

bool MessagesModel::sameGroup(int a, int b) const
{
    if (a < 0 || b < 0 || a >= m_items.size() || b >= m_items.size())
        return false;
    const Item &x = m_items.at(a);
    const Item &y = m_items.at(b);
    if ((x.message.outgoing || x.pending) != (y.message.outgoing || y.pending) || senderOf(x) != senderOf(y))
        return false;
    if (std::abs(x.message.date - y.message.date) > GroupGapMs)
        return false;
    // Reactions hang below a bubble; keep the gap after it.
    if (!y.message.reactions.isEmpty())
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
    for (const SmsAttachment &a : item.message.attachments) {
        QString thumb;
        if (!a.thumbnail.isEmpty())
            thumb = ImageStore::urlFor(QStringLiteral("att/") + a.id);
        else if (m_thumbFiles.contains(a.id))
            thumb = QUrl::fromLocalFile(m_thumbFiles.value(a.id)).toString();
        else if (a.kind() == QLatin1String("image") && m_localFiles.contains(a.id))
            thumb = QUrl::fromLocalFile(m_localFiles.value(a.id)).toString();
        QVariantMap m{{QStringLiteral("kind"), a.kind()},
                      {QStringLiteral("id"), a.id},
                      {QStringLiteral("mimeType"), a.mimeType},
                      {QStringLiteral("name"), a.name.isEmpty() ? a.id : a.name},
                      {QStringLiteral("thumbnail"), thumb},
                      {QStringLiteral("localPath"), m_localFiles.value(a.id)}};
        if (a.isVCard())
            m[QStringLiteral("contact")] = m_vcards.value(a.id);
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
    const SmsMessage &m = item.message;
    switch (role) {
    case Qt::DisplayRole:
    case BodyRole:
        return m.body;
    case DateRole:
        return m.date;
    case OutgoingRole:
        return item.pending || m.outgoing;
    case StatusRole:
        return m.status.isEmpty() ? (m.outgoing ? QStringLiteral("sent") : QStringLiteral("received")) : m.status;
    case SenderRole:
        return senderOf(item);
    case SenderNameRole: {
        const QString s = senderOf(item);
        if (s.isEmpty())
            return m.senderName;
        return m_contacts->isKnown(s) || m.senderName.isEmpty() ? m_contacts->nameFor(s) : m.senderName;
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
    case IdRole:
        return item.pending ? QString() : m.id;
    case ErrorRole:
        return m.error;
    case ReactionsRole:
        return m.reactions;
    case ReplyRole: {
        if (m.replyTo.isEmpty())
            return QVariant();
        const int r = rowOf(m.replyTo);
        if (r < 0)
            return QVariantMap{{QStringLiteral("body"), tr("Earlier message")}, {QStringLiteral("sender"), QString()}};
        const Item &orig = m_items.at(r);
        QString body = orig.message.body;
        if (body.isEmpty() && !orig.message.attachments.isEmpty())
            body = tr("Attachment");
        const QString from = senderOf(orig);
        return QVariantMap{{QStringLiteral("body"), body},
                           {QStringLiteral("sender"), from.isEmpty() ? tr("You") : (orig.message.senderName.isEmpty() ? m_contacts->nameFor(from) : orig.message.senderName)}};
    }
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
        {IdRole, "messageId"},
        {ErrorRole, "error"},
        {ReactionsRole, "reactions"},
        {ReplyRole, "reply"},
    };
}
