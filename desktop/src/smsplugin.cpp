#include "smsplugin.h"
#include "contactstore.h"
#include "messagetypes.h"

#include <QBuffer>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

static const QString KdeConnect = QStringLiteral("kdeconnect");
static const QString GMessages = QStringLiteral("gmessages");

SmsPlugin::SmsPlugin(ContactStore *contacts, QObject *parent)
    : QObject(parent)
    , m_contacts(contacts)
    , m_model(contacts)
    , m_messages(contacts)
{
    m_filter.setSourceModel(&m_model);
    m_filter.setFilterRole(ConversationsModel::SearchRole);

    connect(&m_messages, &MessagesModel::wantAttachment, this, [this](const SmsAttachment &a, bool thumbnail) {
        if (m_backend)
            m_backend->fetchAttachment(a, thumbnail);
    });
    connect(contacts, &ContactStore::contactsChanged, this, &SmsPlugin::currentChanged);
    connect(&m_gm, &GMessagesBackend::statusChanged, this, &SmsPlugin::simsChanged);
    // While Google Messages is the backend, its contacts help the picker and names.
    connect(&m_gm, &GMessagesBackend::contactsLoaded, this, [this](const QVariantList &list) {
        if (m_backend != &m_gm)
            return;
        QList<Contact> contacts;
        for (const QVariant &v : list) {
            const QVariantMap c = v.toMap();
            const QString number = c.value(QStringLiteral("number")).toString();
            if (number.isEmpty())
                continue;
            Contact contact;
            contact.uid = QStringLiteral("gm:") + c.value(QStringLiteral("id")).toString();
            contact.name = c.value(QStringLiteral("name")).toString();
            contact.numbers = {number};
            // So it can be shared as a contact card like synced ones.
            QString fn = contact.name;
            fn.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char(','), QLatin1String("\\,")).replace(QLatin1Char(';'), QLatin1String("\\;"));
            contact.raw = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:" + fn.toUtf8() + "\r\nTEL:" + number.toUtf8() + "\r\nEND:VCARD\r\n";
            contacts << contact;
        }
        m_contacts->setExternal(contacts);
    });

    QString name = qEnvironmentVariable("SLONNECT_MESSAGES_BACKEND");
    if (name.isEmpty())
        name = QSettings().value(QStringLiteral("messages/backend"), KdeConnect).toString();
    m_backendName = name == GMessages ? GMessages : KdeConnect;
    bind(m_backendName == GMessages ? static_cast<MessagingBackend *>(&m_gm) : &m_kde);
}

void SmsPlugin::bind(MessagingBackend *backend)
{
    if (m_backend)
        disconnect(m_backend, nullptr, this, nullptr);
    m_backend = backend;
    connect(backend, &MessagingBackend::readyChanged, this, [this] {
        Q_EMIT availableChanged();
        if (available())
            refresh();
        else if (m_loadingMessages)
            setLoadingMessages(false);
    });
    connect(backend, &MessagingBackend::loadingThreadsChanged, this, [this](bool loading) {
        m_loadingConversations = loading;
        Q_EMIT loadingChanged();
        if (!loading && threadOpen())
            Q_EMIT currentChanged();
    });
    connect(backend, &MessagingBackend::threadChanged, this, &SmsPlugin::onThread);
    connect(backend, &MessagingBackend::threadRemoved, this, [this](const QString &id) {
        m_model.remove(id);
        if (id == m_currentThread)
            close();
    });
    connect(backend, &MessagingBackend::messageChanged, this, &SmsPlugin::onMessage);
    connect(backend, &MessagingBackend::messageRemoved, this, [this](const QString &threadId, const QString &messageId) {
        if (threadId == m_currentThread)
            m_messages.remove(messageId);
    });
    connect(backend, &MessagingBackend::pageLoaded, this, &SmsPlugin::onPageLoaded);
    connect(backend, &MessagingBackend::attachmentReady, this, &SmsPlugin::onAttachment);
    connect(backend, &MessagingBackend::typing, this, &SmsPlugin::onTyping);
    connect(backend, &MessagingBackend::sendFailed, this, [this](const QString &tmpId, const QString &error) {
        m_messages.markFailed(tmpId, error);
        Q_EMIT sendFailed(error);
    });
    if (backend == &m_gm)
        m_gm.ensureRunning();
}

void SmsPlugin::setDevice(const QString &deviceId, const QStringList &loadedPlugins)
{
    m_kde.setDevice(deviceId, loadedPlugins);
}

bool SmsPlugin::available() const
{
    if (m_demo && m_backend == &m_kde)
        return true;
    return m_backend->ready();
}

void SmsPlugin::setBackend(const QString &name)
{
    const QString wanted = name == GMessages ? GMessages : KdeConnect;
    if (wanted == m_backendName)
        return;
    close();
    m_backend->closeThread();
    m_model.clear();
    m_contacts->setExternal({});
    m_backendName = wanted;
    QSettings().setValue(QStringLiteral("messages/backend"), wanted);
    bind(wanted == GMessages ? static_cast<MessagingBackend *>(&m_gm) : &m_kde);
    if (m_demo && m_backend == &m_kde)
        loadDemo();
    if (m_backend == &m_gm && m_gm.ready())
        m_gm.loadContacts();
    Q_EMIT backendChanged();
    Q_EMIT availableChanged();
    Q_EMIT simsChanged();
    refresh();
}

void SmsPlugin::refresh()
{
    if (available() && !(m_demo && m_backend == &m_kde))
        m_backend->refresh();
}

QString SmsPlugin::currentTitle() const
{
    if (const SmsThread *t = m_model.find(m_currentThread))
        return m_model.titleFor(*t);
    return m_model.titleFor(m_currentAddresses);
}

QString SmsPlugin::currentAvatar() const
{
    if (m_currentAddresses.size() != 1)
        return QString();
    const QString mine = m_contacts->avatarFor(m_currentAddresses.first());
    if (!mine.isEmpty())
        return mine;
    const SmsThread *t = m_model.find(m_currentThread);
    return t ? t->avatar : QString();
}

bool SmsPlugin::currentRcs() const
{
    const SmsThread *t = m_model.find(m_currentThread);
    return t && t->rcs;
}

void SmsPlugin::onThread(const SmsThread &t)
{
    m_model.upsert(t);
    // The draft we just sent created this thread; switch over to it.
    if (composing() && m_model.threadFor(m_currentAddresses) == t.id)
        openThread(t.id);
    else if (t.id == m_currentThread)
        Q_EMIT currentChanged(); // title, RCS
}

void SmsPlugin::onMessage(const SmsMessage &m, bool live)
{
    if (m.threadId == m_currentThread) {
        m_messages.add(m);
        if (live && !m.outgoing) {
            // They stopped typing when the message landed.
            if (QTimer *t = m_typing.take(m.sender)) {
                t->deleteLater();
                Q_EMIT typingChanged();
            }
            markOpenThreadRead();
        }
    } else if (composing() && m.outgoing && !m.threadId.isEmpty() && m_model.threadFor(m_currentAddresses) == m.threadId) {
        openThread(m.threadId);
    }
    if (live && !m.outgoing)
        Q_EMIT messageArrived(m.threadId, m.senderName.isEmpty() ? m_contacts->nameFor(m.sender) : m.senderName, m.body);
}

void SmsPlugin::openThread(const QString &threadId)
{
    const SmsThread *head = m_model.find(threadId);
    if (threadId != m_currentThread)
        clearReply();
    m_currentThread = threadId;
    m_currentAddresses = head ? head->addresses : QStringList();
    m_reachedStart = false;
    m_messages.reset(threadId, head && head->multi);
    qDeleteAll(m_typing);
    m_typing.clear();
    Q_EMIT typingChanged();
    Q_EMIT currentChanged();
    if (m_demo && m_backend == &m_kde) {
        for (const auto &m : std::as_const(m_demoMessages))
            if (m.threadId == threadId)
                m_messages.add(m);
        m_reachedStart = true;
        // Serve the demo contact card as if the phone had sent it.
        const QString card = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/ranger.vcf");
        QDir().mkpath(QFileInfo(card).absolutePath());
        QFile f(card);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write("BEGIN:VCARD\nVERSION:3.0\nFN:Park Ranger Station\nTEL;TYPE=WORK:+1 555 010 0099\nEND:VCARD\n");
        f.close();
        m_messages.attachmentDownloaded(QStringLiteral("ranger.vcf"), false, card);
        return;
    }
    setLoadingMessages(true);
    m_backend->openThread(threadId);
}

void SmsPlugin::startConversation(const QStringList &addresses)
{
    QStringList cleaned;
    for (const QString &a : addresses)
        if (!a.trimmed().isEmpty())
            cleaned << a.trimmed();
    if (cleaned.isEmpty())
        return;
    const QString existing = m_model.threadFor(cleaned);
    if (!existing.isEmpty()) {
        openThread(existing);
        return;
    }
    clearReply();
    m_currentThread.clear();
    m_currentAddresses = cleaned;
    m_reachedStart = true;
    m_messages.reset(QString(), cleaned.size() > 1);
    m_backend->closeThread();
    Q_EMIT currentChanged();
}

void SmsPlugin::close()
{
    m_currentThread.clear();
    m_currentAddresses.clear();
    m_messages.reset(QString(), false);
    m_backend->closeThread();
    clearReply();
    setLoadingMessages(false);
    Q_EMIT currentChanged();
}

void SmsPlugin::loadOlder()
{
    if (m_loadingMessages || m_reachedStart || m_currentThread.isEmpty() || !available())
        return;
    setLoadingMessages(true);
    m_backend->loadOlder(m_currentThread);
}

void SmsPlugin::onPageLoaded(const QString &threadId, bool reachedStart)
{
    if (threadId != m_currentThread)
        return;
    const bool first = m_messages.realCount() > 0 && !m_reachedStart && m_loadingMessages;
    m_reachedStart = reachedStart;
    setLoadingMessages(false);
    if (first)
        markOpenThreadRead();
}

void SmsPlugin::markOpenThreadRead()
{
    if (!(m_backend->capabilities() & MessagingBackend::ReadReceipts) || m_currentThread.isEmpty())
        return;
    const SmsThread *t = m_model.find(m_currentThread);
    const QString newest = m_messages.newestIncomingId();
    if (!newest.isEmpty() && (!t || !t->read))
        m_backend->markRead(m_currentThread, newest);
}

void SmsPlugin::setLoadingMessages(bool loading)
{
    if (loading != m_loadingMessages) {
        m_loadingMessages = loading;
        Q_EMIT loadingChanged();
    }
}

static QString toLocalPath(const QVariant &v)
{
    const QString s = v.metaType() == QMetaType::fromType<QUrl>() ? v.toUrl().toString() : v.toString();
    return s.startsWith(QLatin1String("file:")) ? QUrl(s).toLocalFile() : s;
}

bool SmsPlugin::send(const QString &text, const QVariantList &attachments)
{
    if (!available())
        return false;
    QStringList paths;
    for (const QVariant &v : attachments) {
        const QString p = toLocalPath(v);
        if (QFileInfo(p).isFile())
            paths << p;
    }
    const QString body = text.trimmed();
    if (body.isEmpty() && paths.isEmpty())
        return false;
    if (m_currentThread.isEmpty() && m_currentAddresses.isEmpty())
        return false;

    const QString tmpId = QStringLiteral("tmp_") + QUuid::createUuid().toString(QUuid::Id128).left(16);
    const QString replyTo = m_replyTo.value(QStringLiteral("id")).toString();
    if (!(m_demo && m_backend == &m_kde))
        m_backend->send(m_currentThread, m_currentAddresses, body, paths, tmpId, replyTo, m_sim);
    qCDebug(lcSms) << "send: existingThread" << !m_currentThread.isEmpty() << "len" << body.size() << "attachments" << paths.size();
    m_messages.addPending(body, paths, tmpId);
    clearReply();
    return true;
}

void SmsPlugin::openAttachment(const QString &attachmentId)
{
    const QString local = m_messages.localPathFor(attachmentId);
    if (!local.isEmpty() && QFileInfo::exists(local)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(local));
        return;
    }
    const SmsAttachment *a = m_messages.findAttachment(attachmentId);
    if (!a)
        return;
    m_openWhenReady.insert(attachmentId);
    m_backend->fetchAttachment(*a, false);
}

void SmsPlugin::onAttachment(const QString &attachmentId, bool thumbnail, const QString &path)
{
    if (path.isEmpty())
        return;
    m_messages.attachmentDownloaded(attachmentId, thumbnail, path);
    if (!thumbnail && m_openWhenReady.remove(attachmentId))
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void SmsPlugin::react(const QString &messageId, const QString &emoji)
{
    const SmsMessage *m = m_messages.findMessage(messageId);
    if (!m || !canReact())
        return;
    // Toggle: tapping our own reaction again takes it back.
    bool mine = false;
    for (const QVariant &v : m->reactions) {
        const QVariantMap r = v.toMap();
        if (r.value(QStringLiteral("emoji")).toString() == emoji && r.value(QStringLiteral("mine")).toBool())
            mine = true;
    }
    m_backend->react(messageId, emoji, !mine);
}

void SmsPlugin::setReplyTo(const QString &messageId)
{
    const SmsMessage *m = m_messages.findMessage(messageId);
    if (!m || !canReply())
        return;
    QString body = m->body;
    if (body.isEmpty() && !m->attachments.isEmpty())
        body = tr("Attachment");
    const QString sender = m->outgoing ? tr("You") : (m->senderName.isEmpty() ? m_contacts->nameFor(m->sender) : m->senderName);
    m_replyTo = {{QStringLiteral("id"), messageId}, {QStringLiteral("body"), body}, {QStringLiteral("sender"), sender}};
    Q_EMIT replyToChanged();
}

void SmsPlugin::clearReply()
{
    if (m_replyTo.isEmpty())
        return;
    m_replyTo.clear();
    Q_EMIT replyToChanged();
}

void SmsPlugin::userTyping()
{
    if (m_currentThread.isEmpty() || !(m_backend->capabilities() & MessagingBackend::Typing) || !available())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastTypingSent < 5000)
        return;
    m_lastTypingSent = now;
    m_backend->setTyping(m_currentThread);
}

void SmsPlugin::onTyping(const QString &threadId, const QString &address, bool typing)
{
    if (threadId != m_currentThread)
        return;
    QTimer *t = m_typing.value(address);
    if (!typing) {
        if (t) {
            m_typing.remove(address);
            t->deleteLater();
            Q_EMIT typingChanged();
        }
        return;
    }
    if (!t) {
        // Typing indicators expire on their own if the stop never arrives.
        t = new QTimer(this);
        t->setSingleShot(true);
        connect(t, &QTimer::timeout, this, [this, address] {
            if (QTimer *x = m_typing.take(address)) {
                x->deleteLater();
                Q_EMIT typingChanged();
            }
        });
        m_typing.insert(address, t);
        Q_EMIT typingChanged();
    }
    t->start(15000);
}

QStringList SmsPlugin::typing() const
{
    QStringList names;
    for (auto it = m_typing.cbegin(); it != m_typing.cend(); ++it) {
        // In a 1:1 thread the title is the best name (the backend may know it when
        // our synced contacts don't).
        if (m_currentAddresses.size() == 1 && !m_contacts->isKnown(it.key()))
            names << currentTitle();
        else
            names << m_contacts->nameFor(it.key());
    }
    return names;
}

QVariantList SmsPlugin::sims() const
{
    return m_backend == &m_gm ? m_gm.sims() : QVariantList();
}

void SmsPlugin::setSim(const QString &sim)
{
    if (sim == m_sim)
        return;
    m_sim = sim;
    Q_EMIT simsChanged();
}

void SmsPlugin::loadDemo()
{
    m_demo = true;
    if (m_backend != &m_kde)
        return; // the Google Messages backend gets its demo data from `slonnect-gm --demo`
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 min = 60 * 1000;
    qint32 uid = 1;
    auto msg = [&](qint64 thread, const QStringList &addrs, int type, qint64 ago, const QString &body, QList<MessageAttachment> atts = {}) {
        ConversationMessage m;
        m.threadID = thread;
        for (const QString &a : addrs)
            m.addresses << ConversationAddress{a};
        m.eventField = ConversationMessage::TextMessage | (addrs.size() > 1 ? ConversationMessage::MultiTarget : 0);
        m.type = type;
        m.read = 1;
        m.date = now - ago;
        m.body = body;
        m.uID = uid++;
        m.attachments = atts;
        return m;
    };
    const QStringList alex{QStringLiteral("+15550100001")};
    const QStringList group{QStringLiteral("+15550100002"), QStringLiteral("+15550100003"), QStringLiteral("+15550100004")};
    const QStringList rcs{QStringLiteral("example_airline_abcd1234_agent@rbm.goog")};

    // A tiny generated thumbnail for the image attachment.
    QImage img(160, 120, QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            img.setPixel(x, y, qRgb(80 + x / 2, 60 + y, 160));
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");

    const QList<ConversationMessage> thread1{
        msg(1, alex, 1, 3 * 24 * 60 * min, QStringLiteral("Hey! Are we still on for Saturday?")),
        msg(1, alex, 2, 3 * 24 * 60 * min - 2 * min, QStringLiteral("Yes, 10am at the trailhead")),
        msg(1, alex, 1, 60 * min, QStringLiteral("Here's the map https://example.com/trail")),
        msg(1, alex, 1, 59 * min, QString(), {MessageAttachment{101, QStringLiteral("image/png"), QString::fromLatin1(png.toBase64()), QStringLiteral("map.png")}}),
        msg(1, alex, 1, 58 * min, QString(), {MessageAttachment{102, QStringLiteral("text/x-vcard"), QString(), QStringLiteral("ranger.vcf")}}),
        msg(1, alex, 2, 5 * min, QStringLiteral("Perfect, thanks!")),
        msg(1, alex, 2, 4 * min, QStringLiteral("I'll bring snacks 🥨")),
    };
    const QList<ConversationMessage> thread2{
        msg(2, group, 1, 26 * 60 * min, QStringLiteral("Who's bringing the projector?")),
        msg(2, group, 2, 25 * 60 * min, QStringLiteral("I can")),
        msg(2, {group[1], group[0], group[2]}, 1, 24 * 60 * min, QStringLiteral("Great, see you all tomorrow")),
    };
    const QList<ConversationMessage> thread3{
        msg(3, rcs, 1, 4 * 24 * 60 * min, QStringLiteral("Your flight to Portland is boarding at gate D15.")),
    };
    m_demoMessages.clear();
    for (const auto &m : thread1 + thread2 + thread3) {
        m_model.upsert(threadFromKde(m));
        m_demoMessages << smsFromKde(m);
    }
}
