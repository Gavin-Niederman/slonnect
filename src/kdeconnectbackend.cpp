#include "kdeconnectbackend.h"
#include "kdeconnect.h"
#include "messagetypes.h"

#include <QFileInfo>

static constexpr int PageSize = 40;

static const char *const Signals[][2] = {
    {"conversationCreated", SLOT(onConversationCreated(QDBusVariant))},
    {"conversationUpdated", SLOT(onConversationUpdated(QDBusVariant))},
    {"conversationRemoved", SLOT(onConversationRemoved(qlonglong))},
    {"conversationLoaded", SLOT(onConversationLoaded(qlonglong, qulonglong))},
    {"attachmentReceived", SLOT(onAttachmentReceived(QString, QString))},
};

KdeConnectBackend::Plugin::Plugin(KdeConnectBackend *owner)
    : DevicePlugin(QStringLiteral("kdeconnect_sms"), QString(), QStringLiteral("org.kde.kdeconnect.device.conversations"), owner)
    , m_owner(owner)
{
}

void KdeConnectBackend::Plugin::attached()
{
    m_owner->attached();
}

void KdeConnectBackend::Plugin::detached()
{
    m_owner->detached();
}

KdeConnectBackend::KdeConnectBackend(QObject *parent)
    : MessagingBackend(parent)
    , m_plugin(this)
{
    connect(&m_plugin, &DevicePlugin::availableChanged, this, &MessagingBackend::readyChanged);

    // History arrives as a burst of signals with no end marker when the daemon serves
    // it from its cache; treat a short silence as "done".
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        if (!m_paging)
            return;
        m_paging = false;
        Q_EMIT pageLoaded(m_thread, m_delivered == m_countAtRequest);
    });
}

void KdeConnectBackend::attached()
{
    for (const auto &s : Signals)
        kdc::bus().connect(kdc::Service, m_plugin.path(), m_plugin.iface(), QLatin1String(s[0]), this, s[1]);
    refresh();
}

void KdeConnectBackend::detached()
{
    // The device's path is still the old one here; DevicePlugin clears it afterwards.
    for (const auto &s : Signals)
        kdc::bus().disconnect(kdc::Service, m_plugin.path(), m_plugin.iface(), QLatin1String(s[0]), this, s[1]);
    m_heads.clear();
    m_paging = false;
    m_idleTimer.stop();
}

void KdeConnectBackend::refresh()
{
    if (!ready())
        return;
    Q_EMIT loadingThreadsChanged(true);
    kdc::call(m_plugin.path(), m_plugin.iface(), QStringLiteral("activeConversations"), {}, this, [this](const QDBusMessage &reply) {
        QVariantList list;
        const QVariant arg = reply.arguments().value(0);
        if (arg.canConvert<QDBusArgument>())
            arg.value<QDBusArgument>() >> list;
        else
            list = arg.toList();
        for (const QVariant &v : std::as_const(list)) {
            ConversationMessage m;
            if (toMessage(v, m)) {
                const SmsThread t = threadFromKde(m);
                m_heads[t.id] = qMax(m_heads.value(t.id), t.date);
                Q_EMIT threadChanged(t);
            }
        }
        Q_EMIT loadingThreadsChanged(false);
    });
    // Ask the phone for anything newer; results stream in via conversationCreated/Updated.
    m_plugin.invoke(QStringLiteral("requestAllConversationThreads"));
}

void KdeConnectBackend::openThread(const QString &threadId)
{
    m_thread = threadId;
    m_delivered = 0;
    requestPage();
}

void KdeConnectBackend::loadOlder(const QString &threadId)
{
    if (threadId == m_thread && !m_paging)
        requestPage();
}

void KdeConnectBackend::closeThread()
{
    m_thread.clear();
    m_paging = false;
    m_idleTimer.stop();
}

void KdeConnectBackend::requestPage()
{
    if (!ready() || m_thread.isEmpty())
        return;
    m_countAtRequest = m_delivered;
    m_paging = true;
    // The phone may need a while when the daemon's cache is cold.
    m_idleTimer.start(4000);
    m_plugin.invoke(QStringLiteral("requestConversation"), {m_thread.toLongLong(), m_countAtRequest, m_countAtRequest + PageSize});
}

void KdeConnectBackend::send(const QString &threadId, const QStringList &addresses, const QString &body, const QStringList &files, const QString &,
                             const QString &, const QString &)
{
    QVariantList paths; // the daemon opens these with QFile, so plain paths
    for (const QString &f : files)
        paths << f;
    if (!threadId.isEmpty()) {
        m_plugin.invoke(QStringLiteral("replyToConversation"), {threadId.toLongLong(), body, paths});
    } else {
        QVariantList addrs;
        for (const QString &a : addresses)
            addrs << QVariant::fromValue(ConversationAddress{a});
        m_plugin.invoke(QStringLiteral("sendWithoutConversation"), {addrs, body, paths});
    }
}

void KdeConnectBackend::fetchAttachment(const SmsAttachment &attachment, bool thumbnail)
{
    if (thumbnail || attachment.kdePartId < 0)
        return; // KDE Connect sends thumbnails inline
    m_plugin.invoke(QStringLiteral("requestAttachmentFile"), {attachment.kdePartId, attachment.id});
}

void KdeConnectBackend::handle(const ConversationMessage &m, bool created)
{
    const SmsMessage s = smsFromKde(m);
    const qint64 previousHead = m_heads.value(s.threadId, -1);
    const bool newest = s.date > previousHead;
    if (newest)
        m_heads[s.threadId] = s.date;
    Q_EMIT threadChanged(threadFromKde(m));
    if (s.outgoing)
        qCDebug(lcSms) << "outgoing from daemon: created" << created << "currentThread" << (s.threadId == m_thread) << "status" << s.status
                       << "len" << s.body.size();
    if (s.threadId == m_thread) {
        ++m_delivered;
        if (m_paging)
            m_idleTimer.start(350);
    }
    // Updates to a thread's newest message are arrivals; everything else is history.
    Q_EMIT messageChanged(s, newest && !created && previousHead >= 0);
}

void KdeConnectBackend::onConversationCreated(const QDBusVariant &msg)
{
    ConversationMessage m;
    if (toMessage(QVariant::fromValue(msg), m))
        handle(m, true);
}

void KdeConnectBackend::onConversationUpdated(const QDBusVariant &msg)
{
    ConversationMessage m;
    if (toMessage(QVariant::fromValue(msg), m))
        handle(m, false);
}

void KdeConnectBackend::onConversationRemoved(qlonglong threadId)
{
    m_heads.remove(QString::number(threadId));
    Q_EMIT threadRemoved(QString::number(threadId));
}

void KdeConnectBackend::onConversationLoaded(qlonglong threadId, qulonglong)
{
    if (QString::number(threadId) == m_thread && m_paging)
        m_idleTimer.start(350);
}

void KdeConnectBackend::onAttachmentReceived(const QString &filePath, const QString &fileName)
{
    if (QFileInfo::exists(filePath))
        Q_EMIT attachmentReady(fileName, false, filePath);
}
