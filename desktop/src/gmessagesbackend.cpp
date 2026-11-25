#include "gmessagesbackend.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSettings>
#include <QUrl>

Q_LOGGING_CATEGORY(lcGm, "slonnect.gmessages")

static constexpr int PageSize = 40;

static QVariantMap parseObject(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).object().toVariantMap();
}

GMessagesBackend::GMessagesBackend(QObject *parent)
    : MessagingBackend(parent)
{
    m_autoReactivate = QSettings().value(QStringLiteral("gmessages/autoReactivate"), false).toBool();

    auto bus = QDBusConnection::sessionBus();
    const struct {
        const char *name;
        const char *slot;
    } signalMap[] = {
        {"StatusChanged", SLOT(onStatusChanged(QString))},
        {"PairingEmoji", SLOT(onPairingEmoji(QString, QString))},
        {"PairingFinished", SLOT(onPairingFinished(bool, QString))},
        {"ConversationChanged", SLOT(onConversationChanged(QString))},
        {"ConversationRemoved", SLOT(onConversationRemoved(QString))},
        {"MessageChanged", SLOT(onMessageChanged(QString, bool))},
        {"Typing", SLOT(onTyping(QString, QString, bool))},
        {"SendFailed", SLOT(onSendFailed(QString, QString, QString))},
    };
    for (const auto &s : signalMap)
        bus.connect(Service, Path, Iface, QLatin1String(s.name), this, s.slot);

    m_watcher = new QDBusServiceWatcher(Service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] { setRunning(true); });
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] { setRunning(false); });
    setRunning(bus.interface()->isServiceRegistered(Service));
}

void GMessagesBackend::call(const QString &method, const QVariantList &args, Reply onReply, std::function<void(const QString &)> onError, int timeoutMs)
{
    auto msg = QDBusMessage::createMethodCall(Service, Path, Iface, method);
    msg.setArguments(args);
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, timeoutMs), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [method, onReply, onError](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qCWarning(lcGm) << method << "failed:" << reply.errorName();
            if (onError)
                onError(reply.errorMessage());
            return;
        }
        if (onReply)
            onReply(reply);
    });
}

void GMessagesBackend::setRunning(bool running)
{
    if (running) {
        // Refuse a helper that speaks a different protocol.
        auto msg = QDBusMessage::createMethodCall(Service, Path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        msg.setArguments({Iface, QStringLiteral("ProtocolVersion")});
        auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
            w->deleteLater();
            const uint version = w->reply().arguments().value(0).value<QDBusVariant>().variant().toUInt();
            if (version != ProtocolVersion) {
                qCWarning(lcGm) << "helper speaks protocol" << version << "but we need" << ProtocolVersion;
                m_running = false;
                m_error = tr("The Google Messages helper is a different version than Slonnect. Update both together.");
                Q_EMIT statusChanged();
                Q_EMIT readyChanged();
                return;
            }
            m_running = true;
            call(QStringLiteral("SetAutoReactivate"), {m_autoReactivate});
            fetchStatus();
        });
        return;
    }
    const bool wasReady = ready();
    m_running = false;
    Q_EMIT statusChanged();
    if (wasReady)
        Q_EMIT readyChanged();
}

void GMessagesBackend::ensureRunning()
{
    if (m_running)
        return;
    auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
                                              QStringLiteral("StartServiceByName"));
    msg.setArguments({Service, 0u});
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->reply().type() == QDBusMessage::ErrorMessage) {
            qCInfo(lcGm) << "helper not activatable:" << w->reply().errorName();
            m_error = tr("The Google Messages helper (slonnect-gm) isn't installed or running. With Home Manager, set services.slonnect.googleMessages.enable.");
            Q_EMIT statusChanged();
        }
    });
}

void GMessagesBackend::fetchStatus()
{
    call(QStringLiteral("Status"), {}, [this](const QDBusMessage &r) { onStatusChanged(r.arguments().value(0).toString()); });
}

void GMessagesBackend::onStatusChanged(const QString &json)
{
    const QVariantMap st = parseObject(json);
    const bool wasReady = ready();
    m_running = true;
    m_state = st.value(QStringLiteral("state")).toString();
    m_account = st.value(QStringLiteral("account")).toString();
    m_error = st.value(QStringLiteral("error")).toString();
    m_secrets = st.value(QStringLiteral("secrets")).toString();
    m_sims = st.value(QStringLiteral("sims")).toList();
    if (m_state != QLatin1String("pairing") && !m_emoji.isEmpty()) {
        m_emoji.clear();
        Q_EMIT pairingChanged();
    }
    Q_EMIT statusChanged();
    if (wasReady != ready()) {
        Q_EMIT readyChanged();
        if (ready()) {
            refresh();
            loadContacts();
        }
    }
}

void GMessagesBackend::loadContacts()
{
    call(QStringLiteral("ListContacts"), {}, [this](const QDBusMessage &r) {
        Q_EMIT contactsLoaded(QJsonDocument::fromJson(r.arguments().value(0).toString().toUtf8()).array().toVariantList());
    });
}

void GMessagesBackend::setAutoReactivate(bool on)
{
    if (on == m_autoReactivate)
        return;
    m_autoReactivate = on;
    QSettings().setValue(QStringLiteral("gmessages/autoReactivate"), on);
    if (m_running)
        call(QStringLiteral("SetAutoReactivate"), {on});
    Q_EMIT autoReactivateChanged();
}

// --- login -------------------------------------------------------------------------

void GMessagesBackend::startLogin(const QString &pasted)
{
    m_loginError.clear();
    m_emoji.clear();
    Q_EMIT pairingChanged();
    call(QStringLiteral("StartLogin"), {pasted}, {}, [this](const QString &error) {
        m_loginError = error;
        Q_EMIT pairingChanged();
        Q_EMIT loginFinished(false, error);
    });
}

void GMessagesBackend::cancelLogin()
{
    call(QStringLiteral("CancelLogin"), {});
}

void GMessagesBackend::logout()
{
    call(QStringLiteral("Logout"), {});
}

void GMessagesBackend::takeOver()
{
    call(QStringLiteral("TakeOver"), {});
}

void GMessagesBackend::onPairingEmoji(const QString &emoji, const QString &)
{
    m_emoji = emoji;
    Q_EMIT pairingChanged();
}

void GMessagesBackend::onPairingFinished(bool ok, const QString &error)
{
    m_emoji.clear();
    m_loginError = ok ? QString() : error;
    Q_EMIT pairingChanged();
    Q_EMIT loginFinished(ok, error);
}

// --- conversion --------------------------------------------------------------------

SmsThread GMessagesBackend::toThread(const QVariantMap &c)
{
    SmsThread t;
    t.id = c.value(QStringLiteral("id")).toString();
    t.title = c.value(QStringLiteral("name")).toString();
    for (const QVariant &v : c.value(QStringLiteral("participants")).toList()) {
        const QVariantMap p = v.toMap();
        t.addresses << p.value(QStringLiteral("number")).toString();
        t.participantIds << p.value(QStringLiteral("id")).toString();
    }
    t.snippet = c.value(QStringLiteral("snippet")).toString().simplified();
    t.date = c.value(QStringLiteral("date")).toLongLong();
    t.read = !c.value(QStringLiteral("unread")).toBool();
    t.outgoing = c.value(QStringLiteral("fromMe")).toBool();
    t.multi = c.value(QStringLiteral("group")).toBool() || t.addresses.size() > 1;
    t.rcs = c.value(QStringLiteral("rcs")).toBool();
    if (t.participantIds.size() == 1) {
        const QString pid = t.participantIds.first();
        t.avatar = m_avatars.value(pid);
        if (!m_avatars.contains(pid))
            wantAvatar(pid);
    }
    return t;
}

SmsMessage GMessagesBackend::toMessage(const QVariantMap &m) const
{
    SmsMessage s;
    s.id = m.value(QStringLiteral("id")).toString();
    s.tmpId = m.value(QStringLiteral("tmpId")).toString();
    s.threadId = m.value(QStringLiteral("conversationId")).toString();
    s.body = m.value(QStringLiteral("text")).toString();
    s.date = m.value(QStringLiteral("date")).toLongLong();
    s.outgoing = m.value(QStringLiteral("outgoing")).toBool();
    s.status = m.value(QStringLiteral("status")).toString();
    s.error = m.value(QStringLiteral("error")).toString();
    s.replyTo = m.value(QStringLiteral("replyTo")).toString();
    s.reactions = m.value(QStringLiteral("reactions")).toList();
    const QVariantMap sender = m.value(QStringLiteral("sender")).toMap();
    s.sender = sender.value(QStringLiteral("number")).toString();
    s.senderName = sender.value(QStringLiteral("name")).toString();
    const QVariantMap conv = m_conversations.value(s.threadId);
    for (const QVariant &v : conv.value(QStringLiteral("participants")).toList())
        s.addresses << v.toMap().value(QStringLiteral("number")).toString();
    s.multi = conv.value(QStringLiteral("group")).toBool() || s.addresses.size() > 1;
    for (const QVariant &v : m.value(QStringLiteral("attachments")).toList()) {
        const QVariantMap a = v.toMap();
        SmsAttachment att;
        att.id = a.value(QStringLiteral("id")).toString();
        att.mimeType = a.value(QStringLiteral("mime")).toString();
        att.name = a.value(QStringLiteral("name")).toString();
        att.size = a.value(QStringLiteral("size")).toLongLong();
        att.remoteThumbnail = a.value(QStringLiteral("hasThumb")).toBool();
        s.attachments << att;
    }
    return s;
}

void GMessagesBackend::wantAvatar(const QString &participantId)
{
    if (participantId.isEmpty() || m_avatarRequests.contains(participantId) || !m_running)
        return;
    m_avatarRequests.insert(participantId);
    call(QStringLiteral("Avatar"), {participantId}, [this, participantId](const QDBusMessage &r) {
        const QString path = r.arguments().value(0).toString();
        m_avatars.insert(participantId, path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString());
        if (path.isEmpty())
            return;
        // Re-announce threads that show this person.
        for (auto it = m_conversations.cbegin(); it != m_conversations.cend(); ++it) {
            const QVariantList people = it.value().value(QStringLiteral("participants")).toList();
            if (people.size() == 1 && people.first().toMap().value(QStringLiteral("id")).toString() == participantId)
                Q_EMIT threadChanged(toThread(it.value()));
        }
    });
}

// --- requests ----------------------------------------------------------------------

void GMessagesBackend::refresh()
{
    if (!ready())
        return;
    Q_EMIT loadingThreadsChanged(true);
    call(
        QStringLiteral("ListConversations"), {100u},
        [this](const QDBusMessage &r) {
            const QJsonArray list = QJsonDocument::fromJson(r.arguments().value(0).toString().toUtf8()).array();
            for (const QJsonValue &v : list) {
                const QVariantMap c = v.toObject().toVariantMap();
                m_conversations.insert(c.value(QStringLiteral("id")).toString(), c);
                Q_EMIT threadChanged(toThread(c));
            }
            Q_EMIT loadingThreadsChanged(false);
        },
        [this](const QString &) { Q_EMIT loadingThreadsChanged(false); });
}

void GMessagesBackend::openThread(const QString &threadId)
{
    m_thread = threadId;
    m_cursors.remove(threadId);
    fetchPage(threadId, QString());
}

void GMessagesBackend::loadOlder(const QString &threadId)
{
    const QString cursor = m_cursors.value(threadId);
    if (cursor.isEmpty()) {
        Q_EMIT pageLoaded(threadId, true);
        return;
    }
    fetchPage(threadId, cursor);
}

void GMessagesBackend::fetchPage(const QString &threadId, const QString &cursor)
{
    call(
        QStringLiteral("FetchMessages"), {threadId, uint(PageSize), cursor},
        [this, threadId](const QDBusMessage &r) {
            const QVariantMap page = parseObject(r.arguments().value(0).toString());
            for (const QVariant &v : page.value(QStringLiteral("messages")).toList())
                Q_EMIT messageChanged(toMessage(v.toMap()), false);
            const bool more = page.value(QStringLiteral("more")).toBool();
            if (more)
                m_cursors.insert(threadId, page.value(QStringLiteral("cursor")).toString());
            else
                m_cursors.remove(threadId);
            Q_EMIT pageLoaded(threadId, !more);
        },
        [this, threadId](const QString &) { Q_EMIT pageLoaded(threadId, false); }, 120000);
}

void GMessagesBackend::send(const QString &threadId, const QStringList &addresses, const QString &body, const QStringList &files, const QString &tmpId,
                            const QString &replyTo, const QString &sim)
{
    QJsonObject req{{QStringLiteral("text"), body}, {QStringLiteral("tmpId"), tmpId}};
    if (!threadId.isEmpty())
        req.insert(QStringLiteral("conversationId"), threadId);
    else
        req.insert(QStringLiteral("numbers"), QJsonArray::fromStringList(addresses));
    if (!files.isEmpty())
        req.insert(QStringLiteral("files"), QJsonArray::fromStringList(files));
    if (!replyTo.isEmpty())
        req.insert(QStringLiteral("replyTo"), replyTo);
    if (!sim.isEmpty())
        req.insert(QStringLiteral("sim"), sim);
    call(QStringLiteral("Send"), {QString::fromUtf8(QJsonDocument(req).toJson(QJsonDocument::Compact))}, {},
         [this, tmpId](const QString &error) { Q_EMIT sendFailed(tmpId, error); });
}

void GMessagesBackend::fetchAttachment(const SmsAttachment &attachment, bool thumbnail)
{
    if (thumbnail && !attachment.remoteThumbnail)
        return;
    const QString id = attachment.id;
    call(
        QStringLiteral("FetchMedia"), {id, thumbnail},
        [this, id, thumbnail](const QDBusMessage &r) { Q_EMIT attachmentReady(id, thumbnail, r.arguments().value(0).toString()); }, {}, 300000);
}

void GMessagesBackend::markRead(const QString &threadId, const QString &messageId)
{
    call(QStringLiteral("MarkRead"), {threadId, messageId});
}

void GMessagesBackend::react(const QString &messageId, const QString &emoji, bool add)
{
    call(QStringLiteral("React"), {messageId, emoji, add});
}

void GMessagesBackend::setTyping(const QString &threadId)
{
    call(QStringLiteral("SetTyping"), {threadId});
}

// --- helper signals ----------------------------------------------------------------

void GMessagesBackend::onConversationChanged(const QString &json)
{
    const QVariantMap c = parseObject(json);
    m_conversations.insert(c.value(QStringLiteral("id")).toString(), c);
    Q_EMIT threadChanged(toThread(c));
}

void GMessagesBackend::onConversationRemoved(const QString &id)
{
    m_conversations.remove(id);
    Q_EMIT threadRemoved(id);
}

void GMessagesBackend::onMessageChanged(const QString &json, bool live)
{
    const SmsMessage m = toMessage(parseObject(json));
    if (m.status == QLatin1String("deleted"))
        Q_EMIT messageRemoved(m.threadId, m.id);
    else
        Q_EMIT messageChanged(m, live);
}

void GMessagesBackend::onTyping(const QString &conversationId, const QString &number, bool typing)
{
    Q_EMIT this->typing(conversationId, number, typing);
}

void GMessagesBackend::onSendFailed(const QString &tmpId, const QString &, const QString &error)
{
    Q_EMIT sendFailed(tmpId, error);
}
