#include "notificationsplugin.h"
#include "kdeconnect.h"

#include <QUrl>
#include <algorithm>

static const QString NotificationIface = QStringLiteral("org.kde.kdeconnect.device.notifications.notification");

// --- model -------------------------------------------------------------------

int NotificationsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant NotificationsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case IdRole:
        return e.id;
    case AppNameRole:
        return e.appName;
    case TitleRole:
        return e.title;
    case TextRole:
        return e.text;
    case IconRole:
        return e.iconPath.isEmpty() ? QString() : QUrl::fromLocalFile(e.iconPath).toString();
    case DismissableRole:
        return e.dismissable;
    case RepliableRole:
        return e.repliable;
    case SilentRole:
        return e.silent;
    case ArrivedRole:
        return e.arrived.isValid() ? QVariant(e.arrived.toMSecsSinceEpoch()) : QVariant(0);
    }
    return {};
}

QHash<int, QByteArray> NotificationsModel::roleNames() const
{
    return {{IdRole, "notificationId"}, {AppNameRole, "appName"},        {TitleRole, "title"},         {TextRole, "text"},     {IconRole, "icon"},
            {DismissableRole, "dismissable"}, {RepliableRole, "repliable"}, {SilentRole, "silent"}, {ArrivedRole, "arrived"}};
}

void NotificationsModel::upsert(const Entry &e)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == e.id) {
            Entry updated = e;
            if (!updated.arrived.isValid())
                updated.arrived = m_entries[i].arrived;
            m_entries[i] = updated;
            Q_EMIT dataChanged(index(i), index(i));
            return;
        }
    }
    // Public ids are increasing integers, so they order notifications by arrival.
    const qlonglong key = e.id.toLongLong();
    const auto it = std::find_if(m_entries.cbegin(), m_entries.cend(), [key](const Entry &o) { return o.id.toLongLong() < key; });
    const int row = int(it - m_entries.cbegin());
    beginInsertRows({}, row, row);
    m_entries.insert(row, e);
    endInsertRows();
}

void NotificationsModel::remove(const QString &id)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id) {
            beginRemoveRows({}, i, i);
            m_entries.removeAt(i);
            endRemoveRows();
            return;
        }
    }
}

void NotificationsModel::clear()
{
    if (m_entries.isEmpty())
        return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

// --- plugin ------------------------------------------------------------------

NotificationsPlugin::NotificationsPlugin(QObject *parent)
    : DevicePlugin(QStringLiteral("kdeconnect_notifications"), QStringLiteral("/notifications"), QStringLiteral("org.kde.kdeconnect.device.notifications"), parent)
{
    connect(&m_model, &QAbstractItemModel::rowsInserted, this, &NotificationsPlugin::countChanged);
    connect(&m_model, &QAbstractItemModel::rowsRemoved, this, &NotificationsPlugin::countChanged);
    connect(&m_model, &QAbstractItemModel::modelReset, this, &NotificationsPlugin::countChanged);
    connect(&m_model, &QAbstractItemModel::dataChanged, this, &NotificationsPlugin::countChanged);
}

bool NotificationsPlugin::anyDismissable() const
{
    const auto &entries = m_model.entries();
    return std::any_of(entries.cbegin(), entries.cend(), [](const NotificationsModel::Entry &e) { return e.dismissable; });
}

void NotificationsPlugin::attached()
{
    subscribe(QStringLiteral("notificationPosted"), SLOT(onPosted(QString)));
    subscribe(QStringLiteral("notificationUpdated"), SLOT(onPosted(QString)));
    subscribe(QStringLiteral("notificationRemoved"), SLOT(onRemoved(QString)));
    subscribe(QStringLiteral("allNotificationsRemoved"), SLOT(onAllRemoved()));
    kdc::call(path(), iface(), QStringLiteral("activeNotifications"), {}, this, [this](const QDBusMessage &reply) {
        const QStringList ids = kdc::unwrap(reply.arguments().value(0)).toStringList();
        for (const QString &id : ids)
            fetch(id, false);
    });
}

void NotificationsPlugin::detached()
{
    if (!m_demo)
        m_model.clear();
}

QString NotificationsPlugin::notificationPath(const QString &id) const
{
    return path() + QLatin1Char('/') + id;
}

void NotificationsPlugin::fetch(const QString &id, bool fresh)
{
    const QString device = deviceId();
    kdc::getAll(notificationPath(id), NotificationIface, this, [this, id, fresh, device](const QVariantMap &p) {
        if (device != deviceId() || !available())
            return; // switched devices while the reply was in flight
        NotificationsModel::Entry e;
        e.id = id;
        e.appName = p.value(QStringLiteral("appName")).toString();
        e.title = p.value(QStringLiteral("title")).toString();
        e.text = p.value(QStringLiteral("text")).toString();
        if (p.value(QStringLiteral("hasIcon")).toBool())
            e.iconPath = p.value(QStringLiteral("iconPath")).toString();
        e.dismissable = p.value(QStringLiteral("dismissable")).toBool();
        e.repliable = !p.value(QStringLiteral("replyId")).toString().isEmpty();
        e.silent = p.value(QStringLiteral("silent")).toBool();
        if (fresh)
            e.arrived = QDateTime::currentDateTime();
        // Some apps post an empty placeholder (e.g. "syncing"); nothing to show.
        if (e.title.isEmpty() && e.text.isEmpty())
            return;
        m_model.upsert(e);
    });
}

void NotificationsPlugin::onPosted(const QString &id)
{
    fetch(id, true);
}

void NotificationsPlugin::onRemoved(const QString &id)
{
    m_model.remove(id);
}

void NotificationsPlugin::onAllRemoved()
{
    m_model.clear();
}

void NotificationsPlugin::dismiss(const QString &id)
{
    if (m_demo) {
        m_model.remove(id);
        return;
    }
    if (available())
        kdc::call(notificationPath(id), NotificationIface, QStringLiteral("dismiss"));
}

void NotificationsPlugin::dismissAll()
{
    const auto entries = m_model.entries();
    for (const auto &e : entries)
        if (e.dismissable)
            dismiss(e.id);
}

void NotificationsPlugin::reply(const QString &id, const QString &message)
{
    if (m_demo || message.trimmed().isEmpty() || !available())
        return;
    kdc::call(notificationPath(id), NotificationIface, QStringLiteral("sendReply"), {message});
}

void NotificationsPlugin::loadDemo()
{
    m_demo = true;
    const QDateTime now = QDateTime::currentDateTime();
    auto add = [&](int id, const char *app, const char *title, const char *text, bool repliable, int minutesAgo) {
        NotificationsModel::Entry e;
        e.id = QString::number(id);
        e.appName = QString::fromUtf8(app);
        e.title = QString::fromUtf8(title);
        e.text = QString::fromUtf8(text);
        e.dismissable = true;
        e.repliable = repliable;
        e.arrived = now.addSecs(-60 * minutesAgo);
        m_model.upsert(e);
    };
    add(1, "Calendar", "Trail crew meetup", "Tomorrow 9:00 · North trailhead", false, 95);
    add(2, "Signal", "Robin", "Bringing the spare headlamp 👍", true, 32);
    add(3, "Weather", "Rain this afternoon", "70% chance from 3 PM. Pack a shell.", false, 12);
    add(4, "Messages", "Sam Okafor", "Are we still on for 6?", true, 2);
}
