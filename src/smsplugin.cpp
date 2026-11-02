#include "smsplugin.h"
#include "contactstore.h"
#include "kdeconnect.h"

#include <QBuffer>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QImage>
#include <QFileInfo>
#include <QUrl>

static constexpr int PageSize = 40;

SmsPlugin::SmsPlugin(ContactStore *contacts, QObject *parent)
    : DevicePlugin(QStringLiteral("kdeconnect_sms"), QString(), QStringLiteral("org.kde.kdeconnect.device.conversations"), parent)
    , m_contacts(contacts)
    , m_model(contacts)
    , m_messages(contacts)
{
    m_filter.setSourceModel(&m_model);
    m_filter.setFilterRole(ConversationsModel::SearchRole);

    // History arrives as a burst of signals with no explicit end marker when it
    // is served from the daemon's cache; treat a short silence as "done".
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        if (m_messages.realCount() == m_countAtRequest)
            m_reachedStart = true;
        setLoadingMessages(false);
    });

    connect(&m_messages, &MessagesModel::wantAttachment, this, &SmsPlugin::downloadAttachment);
    connect(contacts, &ContactStore::contactsChanged, this, &SmsPlugin::currentChanged);
}

void SmsPlugin::attached()
{
    subscribe(QStringLiteral("conversationCreated"), SLOT(onConversationCreated(QDBusVariant)));
    subscribe(QStringLiteral("conversationUpdated"), SLOT(onConversationUpdated(QDBusVariant)));
    subscribe(QStringLiteral("conversationRemoved"), SLOT(onConversationRemoved(qlonglong)));
    subscribe(QStringLiteral("conversationLoaded"), SLOT(onConversationLoaded(qlonglong, qulonglong)));
    subscribe(QStringLiteral("attachmentReceived"), SLOT(onAttachmentReceived(QString, QString)));
    refresh();
}

void SmsPlugin::detached()
{
    m_model.clear();
    close();
}

void SmsPlugin::refresh()
{
    if (!available())
        return;
    m_loadingConversations = true;
    Q_EMIT loadingChanged();
    kdc::call(path(), iface(), QStringLiteral("activeConversations"), {}, this, [this](const QDBusMessage &reply) {
        QVariantList list;
        const QVariant arg = reply.arguments().value(0);
        if (arg.canConvert<QDBusArgument>())
            arg.value<QDBusArgument>() >> list;
        else
            list = arg.toList();
        for (const QVariant &v : std::as_const(list)) {
            ConversationMessage m;
            if (toMessage(v, m))
                m_model.upsert(m);
        }
        m_loadingConversations = false;
        Q_EMIT loadingChanged();
        if (m_currentThread >= 0 || composing())
            Q_EMIT currentChanged();
    });
    // Ask the phone for anything newer; results stream in via conversationCreated/Updated.
    invoke(QStringLiteral("requestAllConversationThreads"));
}

QString SmsPlugin::currentTitle() const
{
    return m_model.titleFor(m_currentAddresses);
}

void SmsPlugin::openThread(qint64 threadId)
{
    const ConversationMessage *head = m_model.find(threadId);
    m_currentThread = threadId;
    m_currentAddresses = head ? head->addressList() : QStringList();
    m_reachedStart = false;
    m_messages.reset(threadId, head && (head->isMultiTarget() || head->addresses.size() > 1));
    Q_EMIT currentChanged();
    if (!m_demo.isEmpty()) {
        for (const auto &m : std::as_const(m_demo))
            if (m.threadID == threadId)
                m_messages.add(m);
        m_reachedStart = true;
        // Serve the demo contact card as if the phone had sent it.
        const QString card = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/ranger.vcf");
        QDir().mkpath(QFileInfo(card).absolutePath());
        QFile f(card);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write("BEGIN:VCARD\nVERSION:3.0\nFN:Park Ranger Station\nTEL;TYPE=WORK:+1 555 010 0099\nEND:VCARD\n");
        f.close();
        m_messages.attachmentDownloaded(QStringLiteral("ranger.vcf"), card);
        return;
    }
    requestPage();
}

void SmsPlugin::startConversation(const QStringList &addresses)
{
    QStringList cleaned;
    for (const QString &a : addresses)
        if (!a.trimmed().isEmpty())
            cleaned << a.trimmed();
    if (cleaned.isEmpty())
        return;
    const qint64 existing = m_model.threadFor(cleaned);
    if (existing >= 0) {
        openThread(existing);
        return;
    }
    m_currentThread = -1;
    m_currentAddresses = cleaned;
    m_reachedStart = true;
    m_messages.reset(-1, cleaned.size() > 1);
    Q_EMIT currentChanged();
}

void SmsPlugin::close()
{
    m_currentThread = -1;
    m_currentAddresses.clear();
    m_messages.reset(-1, false);
    setLoadingMessages(false);
    Q_EMIT currentChanged();
}

void SmsPlugin::loadOlder()
{
    if (m_loadingMessages || m_reachedStart || m_currentThread < 0)
        return;
    requestPage();
}

void SmsPlugin::requestPage()
{
    if (!available() || m_currentThread < 0)
        return;
    m_countAtRequest = m_messages.realCount();
    setLoadingMessages(true);
    // The phone may need a while when the daemon's cache is cold.
    m_idleTimer.start(4000);
    invoke(QStringLiteral("requestConversation"), {m_currentThread, m_countAtRequest, m_countAtRequest + PageSize});
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
    QVariantList pathVariants;
    for (const QVariant &v : attachments) {
        const QString p = toLocalPath(v);
        if (QFileInfo(p).isFile()) {
            paths << p;
            pathVariants << p; // the daemon opens these with QFile, so plain paths
        }
    }
    const QString body = text.trimmed();
    if (body.isEmpty() && paths.isEmpty())
        return false;

    if (m_currentThread >= 0) {
        invoke(QStringLiteral("replyToConversation"), {m_currentThread, body, pathVariants});
    } else if (!m_currentAddresses.isEmpty()) {
        QVariantList addrs;
        for (const QString &a : std::as_const(m_currentAddresses))
            addrs << QVariant::fromValue(ConversationAddress{a});
        invoke(QStringLiteral("sendWithoutConversation"), {addrs, body, pathVariants});
    } else {
        return false;
    }
    qCDebug(lcSms) << "send: existingThread" << (m_currentThread >= 0) << "len" << body.size() << "attachments" << paths.size();
    m_messages.addPending(body, paths);
    return true;
}

void SmsPlugin::downloadAttachment(qint64 partId, const QString &uniqueIdentifier)
{
    const QString local = m_messages.localPathFor(uniqueIdentifier);
    if (!local.isEmpty() && QFileInfo::exists(local)) {
        onAttachmentReceived(local, uniqueIdentifier);
        return;
    }
    invoke(QStringLiteral("requestAttachmentFile"), {partId, uniqueIdentifier});
}

void SmsPlugin::openAttachment(qint64 partId, const QString &uniqueIdentifier)
{
    m_openWhenReady.insert(uniqueIdentifier);
    downloadAttachment(partId, uniqueIdentifier);
}

void SmsPlugin::launchPhoneApp()
{
    kdc::call(path() + QStringLiteral("/sms"), QStringLiteral("org.kde.kdeconnect.device.sms"), QStringLiteral("launchApp"));
}

void SmsPlugin::handleMessage(const ConversationMessage &m, bool created)
{
    const ConversationMessage *previousHead = m_model.find(m.threadID);
    const bool isNewHead = !previousHead || previousHead->date < m.date;
    m_model.upsert(m);
    if (m.isOutgoing())
        qCDebug(lcSms) << "outgoing from daemon: created" << created << "currentThread" << (m.threadID == m_currentThread) << "composing" << composing()
                       << "type" << m.type << "len" << m.body.size();

    if (m.threadID == m_currentThread) {
        if (m_messages.add(m) && m_loadingMessages)
            m_idleTimer.start(350);
    } else if (composing() && m_model.threadFor(m_currentAddresses) == m.threadID) {
        // The draft we just sent created this thread; switch over to it.
        openThread(m.threadID);
    }

    if (isNewHead && !created && !m.isOutgoing() && m.read == 0)
        Q_EMIT messageArrived(m.threadID, m_model.titleFor(m.addressList()), m.body);
}

void SmsPlugin::onConversationCreated(const QDBusVariant &msg)
{
    ConversationMessage m;
    if (toMessage(QVariant::fromValue(msg), m))
        handleMessage(m, true);
}

void SmsPlugin::onConversationUpdated(const QDBusVariant &msg)
{
    ConversationMessage m;
    if (toMessage(QVariant::fromValue(msg), m))
        handleMessage(m, false);
}

void SmsPlugin::onConversationRemoved(qlonglong threadId)
{
    m_model.remove(threadId);
    if (threadId == m_currentThread)
        close();
}

void SmsPlugin::onConversationLoaded(qlonglong threadId, qulonglong)
{
    if (threadId == m_currentThread && m_loadingMessages)
        m_idleTimer.start(350);
}

void SmsPlugin::onAttachmentReceived(const QString &filePath, const QString &fileName)
{
    m_messages.attachmentDownloaded(fileName, filePath);
    if (m_openWhenReady.remove(fileName))
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}

void SmsPlugin::loadDemo()
{
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
    for (const auto &m : thread1 + thread2 + thread3)
        m_model.upsert(m);
    m_demo = thread1 + thread2 + thread3;
}
