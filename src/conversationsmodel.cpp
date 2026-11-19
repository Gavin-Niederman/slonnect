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

int ConversationsModel::indexOf(const QString &threadId) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).id == threadId)
            return i;
    return -1;
}

int ConversationsModel::insertPosition(qint64 date) const
{
    auto it = std::lower_bound(m_rows.cbegin(), m_rows.cend(), date, [](const SmsThread &m, qint64 d) { return m.date > d; });
    return int(it - m_rows.cbegin());
}

void ConversationsModel::upsert(const SmsThread &thread)
{
    const int existing = indexOf(thread.id);
    if (existing < 0) {
        const int pos = insertPosition(thread.date);
        beginInsertRows({}, pos, pos);
        m_rows.insert(pos, thread);
        endInsertRows();
        return;
    }
    const SmsThread &old = m_rows.at(existing);
    if (old.date > thread.date)
        return; // an older message for a thread we already have; not the head
    SmsThread merged = thread;
    // Keep what the newer data doesn't carry.
    if (merged.title.isEmpty())
        merged.title = old.title;
    if (merged.avatar.isEmpty())
        merged.avatar = old.avatar;
    if (merged.participantIds.isEmpty())
        merged.participantIds = old.participantIds;
    m_rows[existing] = merged;
    // Find the new slot ignoring the row itself.
    int target = existing;
    while (target > 0 && m_rows.at(target - 1).date < thread.date)
        --target;
    if (target != existing) {
        beginMoveRows({}, existing, existing, {}, target);
        m_rows.move(existing, target);
        endMoveRows();
    }
    Q_EMIT dataChanged(index(target), index(target));
}

void ConversationsModel::remove(const QString &threadId)
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

const SmsThread *ConversationsModel::find(const QString &threadId) const
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

QString ConversationsModel::threadFor(const QStringList &addresses) const
{
    const auto wanted = normalizedSet(addresses);
    for (const auto &t : m_rows)
        if (normalizedSet(t.addresses) == wanted)
            return t.id;
    return QString();
}

QString ConversationsModel::titleFor(const SmsThread &thread) const
{
    // A 1:1 contact we know by name reads best; otherwise the backend's title
    // (group names, names from the phone's own contacts) beats bare numbers.
    if (thread.addresses.size() == 1 && m_contacts->isKnown(thread.addresses.first()))
        return m_contacts->nameFor(thread.addresses.first());
    if (!thread.title.isEmpty())
        return thread.title;
    return titleFor(thread.addresses);
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
    const SmsThread &t = m_rows.at(index.row());
    switch (role) {
    case ThreadIdRole:
        return t.id;
    case BodyRole: {
        if (!t.snippet.isEmpty())
            return t.snippet;
        const QString &mime = t.attachmentMime;
        if (mime.isEmpty())
            return QString();
        if (mime.startsWith(QLatin1String("image/")))
            return tr("Photo");
        if (mime.startsWith(QLatin1String("video/")))
            return tr("Video");
        if (mime.contains(QLatin1String("vcard")))
            return tr("Contact card");
        return tr("Attachment");
    }
    case DateRole:
        return t.date;
    case ReadRole:
        return t.read;
    case OutgoingRole:
        return t.outgoing;
    case AddressesRole:
        return t.addresses;
    case MultiRole:
        return t.multi;
    case Qt::DisplayRole:
    case TitleRole:
        return titleFor(t);
    case AvatarRole: {
        if (t.addresses.size() != 1)
            return QString();
        const QString mine = m_contacts->avatarFor(t.addresses.first());
        return mine.isEmpty() ? t.avatar : mine;
    }
    case AttachmentRole:
        return !t.attachmentMime.isEmpty();
    case SearchRole:
        return titleFor(t) + QLatin1Char('\n') + t.addresses.join(QLatin1Char(' ')) + QLatin1Char('\n') + t.snippet;
    case RcsRole:
        return t.rcs;
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
        {RcsRole, "rcs"},
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
