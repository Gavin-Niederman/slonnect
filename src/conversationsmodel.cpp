#include "conversationsmodel.h"
#include "contactstore.h"

#include <QSet>

ConversationsModel::ConversationsModel(ContactStore *contacts, QObject *parent)
    : QAbstractListModel(parent)
    , m_contacts(contacts)
{
    connect(contacts, &ContactStore::contactsChanged, this, [this] {
        if (!m_rows.isEmpty())
            Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), {TitleRole, AvatarRole, SearchRole});
    });
}

int ConversationsModel::indexOf(qint64 threadId) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).threadID == threadId)
            return i;
    return -1;
}

int ConversationsModel::insertPosition(qint64 date) const
{
    auto it = std::lower_bound(m_rows.cbegin(), m_rows.cend(), date, [](const ConversationMessage &m, qint64 d) { return m.date > d; });
    return int(it - m_rows.cbegin());
}

void ConversationsModel::upsert(const ConversationMessage &message)
{
    const int existing = indexOf(message.threadID);
    if (existing < 0) {
        const int pos = insertPosition(message.date);
        beginInsertRows({}, pos, pos);
        m_rows.insert(pos, message);
        endInsertRows();
        return;
    }
    if (m_rows.at(existing).date > message.date)
        return; // older message for a thread we already have; not the head

    m_rows[existing] = message;
    // Find the new slot ignoring the row itself.
    int target = existing;
    while (target > 0 && m_rows.at(target - 1).date < message.date)
        --target;
    if (target != existing) {
        beginMoveRows({}, existing, existing, {}, target);
        m_rows.move(existing, target);
        endMoveRows();
    }
    Q_EMIT dataChanged(index(target), index(target));
}

void ConversationsModel::remove(qint64 threadId)
{
    const int row = indexOf(threadId);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
}

void ConversationsModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

const ConversationMessage *ConversationsModel::find(qint64 threadId) const
{
    const int row = indexOf(threadId);
    return row < 0 ? nullptr : &m_rows.at(row);
}

static QSet<QString> normalizedSet(const QStringList &addresses)
{
    QSet<QString> set;
    for (const QString &a : addresses)
        set.insert(ContactStore::normalize(a));
    return set;
}

qint64 ConversationsModel::threadFor(const QStringList &addresses) const
{
    const auto wanted = normalizedSet(addresses);
    for (const auto &m : m_rows)
        if (normalizedSet(m.addressList()) == wanted)
            return m.threadID;
    return -1;
}

QString ConversationsModel::titleFor(const QStringList &addresses) const
{
    QStringList names;
    QSet<QString> seen;
    for (const QString &a : addresses) {
        const QString n = ContactStore::normalize(a);
        if (seen.contains(n))
            continue;
        seen.insert(n);
        QString name = m_contacts->nameFor(a);
        // First names keep group titles readable.
        if (addresses.size() > 1 && m_contacts->isKnown(a))
            name = name.section(QLatin1Char(' '), 0, 0);
        names << name;
    }
    return names.join(QStringLiteral(", "));
}

int ConversationsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ConversationsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const ConversationMessage &m = m_rows.at(index.row());
    switch (role) {
    case ThreadIdRole:
        return m.threadID;
    case BodyRole: {
        if (!m.body.isEmpty())
            return m.body.simplified();
        if (!m.attachments.isEmpty()) {
            const QString mime = m.attachments.first().mimeType;
            if (mime.startsWith(QLatin1String("image/")))
                return tr("Photo");
            if (mime.startsWith(QLatin1String("video/")))
                return tr("Video");
            if (mime.contains(QLatin1String("vcard")))
                return tr("Contact card");
            return tr("Attachment");
        }
        return QString();
    }
    case DateRole:
        return m.date;
    case ReadRole:
        return m.read != 0;
    case OutgoingRole:
        return m.isOutgoing();
    case AddressesRole:
        return m.addressList();
    case MultiRole:
        return m.isMultiTarget() || m.addresses.size() > 1;
    case Qt::DisplayRole:
    case TitleRole:
        return titleFor(m.addressList());
    case AvatarRole:
        return m.addresses.size() == 1 ? m_contacts->avatarFor(m.addresses.first().address) : QString();
    case AttachmentRole:
        return !m.attachments.isEmpty();
    case SearchRole:
        return titleFor(m.addressList()) + QLatin1Char('\n') + m.addressList().join(QLatin1Char(' ')) + QLatin1Char('\n') + m.body;
    }
    return {};
}

QHash<int, QByteArray> ConversationsModel::roleNames() const
{
    return {
        {ThreadIdRole, "threadId"},
        {BodyRole, "body"},
        {DateRole, "date"},
        {ReadRole, "read"},
        {OutgoingRole, "outgoing"},
        {AddressesRole, "addresses"},
        {MultiRole, "multi"},
        {TitleRole, "title"},
        {AvatarRole, "avatar"},
        {AttachmentRole, "hasAttachment"},
    };
}

void ConversationsFilter::setSearch(const QString &text)
{
    if (text == m_search)
        return;
    m_search = text;
    setFilterRegularExpression(QRegularExpression(QRegularExpression::escape(text), QRegularExpression::CaseInsensitiveOption));
    Q_EMIT searchChanged();
}
