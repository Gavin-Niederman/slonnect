#include "contactstore.h"
#include "imagestore.h"
#include "kdeconnect.h"

#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrent>

static const QString ContactsIface = QStringLiteral("org.kde.kdeconnect.device.contacts");

ContactStore::ContactStore(QObject *parent)
    : QAbstractListModel(parent)
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(300);
    connect(&m_reloadTimer, &QTimer::timeout, this, &ContactStore::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reloadTimer, qOverload<>(&QTimer::start));

    m_filtered.setSourceModel(this);
    m_filtered.setFilterRole(SearchRole);
    m_filtered.setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_filtered.setSortRole(NameRole);
    m_filtered.setSortCaseSensitivity(Qt::CaseInsensitive);
    m_filtered.setSortLocaleAware(true);
    m_filtered.sort(0);
}

void ContactStore::setDevice(const QString &deviceId, bool contactsPluginLoaded)
{
    if (deviceId == m_deviceId && contactsPluginLoaded == m_pluginLoaded)
        return;
    const QString contactsPath = kdc::devicePath(m_deviceId) + QStringLiteral("/contacts");
    if (m_pluginLoaded)
        kdc::bus().disconnect(kdc::Service, contactsPath, ContactsIface, QStringLiteral("localCacheSynchronized"), this,
                              SLOT(onLocalCacheSynchronized()));
    const bool deviceChanged = deviceId != m_deviceId;
    m_deviceId = deviceId;
    m_pluginLoaded = contactsPluginLoaded;
    if (m_pluginLoaded)
        kdc::bus().connect(kdc::Service, kdc::devicePath(m_deviceId) + QStringLiteral("/contacts"), ContactsIface,
                           QStringLiteral("localCacheSynchronized"), this, SLOT(onLocalCacheSynchronized()));

    if (!deviceChanged)
        return;
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    m_dir.clear();
    if (!deviceId.isEmpty()) {
        m_dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kpeoplevcard/kdeconnect-") + deviceId;
        QDir().mkpath(m_dir);
        m_watcher.addPath(m_dir);
    }
    reload();
    // Pull anything new from the phone in the background.
    if (m_pluginLoaded)
        sync();
}

void ContactStore::sync()
{
    if (!m_pluginLoaded)
        return;
    m_phoneSilent = false;
    if (!m_syncing) {
        m_syncing = true;
        Q_EMIT syncingChanged();
    }
    kdc::call(kdc::devicePath(m_deviceId) + QStringLiteral("/contacts"), ContactsIface, QStringLiteral("synchronizeRemoteWithLocal"));
    // The phone may never answer (its Contacts synchronizer plugin is off, or
    // permission is missing), so don't spin forever.
    QTimer::singleShot(15000, this, [this] {
        if (m_syncing) {
            m_syncing = false;
            m_phoneSilent = true;
            Q_EMIT syncingChanged();
        }
    });
}

void ContactStore::onLocalCacheSynchronized()
{
    m_syncing = false;
    m_phoneSilent = false;
    Q_EMIT syncingChanged();
    m_reloadTimer.start();
}

void ContactStore::reload()
{
    const quint64 generation = ++m_generation;
    const QString dir = m_dir;
    auto *watcher = new QFutureWatcher<QList<Contact>>(this);
    connect(watcher, &QFutureWatcher<QList<Contact>>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_generation)
            return; // superseded by a newer reload / device switch
        beginResetModel();
        m_contacts = watcher->result();
        m_byNumber.clear();
        for (int i = 0; i < m_contacts.size(); ++i) {
            for (const QString &n : std::as_const(m_contacts[i].numbers))
                m_byNumber.insert(normalize(n), i);
            for (const QString &e : std::as_const(m_contacts[i].emails))
                m_byNumber.insert(normalize(e), i);
        }
        endResetModel();
        Q_EMIT countChanged();
        Q_EMIT contactsChanged();
    });
    watcher->setFuture(QtConcurrent::run([dir] {
        QList<Contact> result;
        if (dir.isEmpty())
            return result;
        const auto files = QDir(dir).entryInfoList({QStringLiteral("*.vcf"), QStringLiteral("*.vcard")}, QDir::Files);
        result.reserve(files.size());
        for (const QFileInfo &fi : files) {
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly))
                continue;
            Contact c = parseVCard(f.readAll(), fi.completeBaseName());
            if (c.name.isEmpty() && c.numbers.isEmpty())
                continue;
            c.uid = fi.completeBaseName();
            c.filePath = fi.absoluteFilePath();
            result << c;
        }
        return result;
    }));
}

static QByteArray unfold(const QByteArray &data)
{
    QByteArray out = data;
    out.replace("\r\n", "\n");
    out.replace("\n ", "");
    out.replace("\n\t", "");
    return out;
}

static QString unescape(QString v)
{
    v.replace(QLatin1String("\\n"), QLatin1String("\n"));
    v.replace(QLatin1String("\\,"), QLatin1String(","));
    v.replace(QLatin1String("\\;"), QLatin1String(";"));
    v.replace(QLatin1String("\\\\"), QLatin1String("\\"));
    return v;
}

Contact ContactStore::parseVCard(const QByteArray &data, const QString &photoKeyHint)
{
    Contact c;
    c.raw = data;
    QString structuredName;
    QByteArray photo;
    const auto lines = unfold(data).split('\n');
    for (const QByteArray &rawLine : lines) {
        const int colon = rawLine.indexOf(':');
        if (colon <= 0)
            continue;
        const QByteArray head = rawLine.left(colon).toUpper();
        const QByteArray prop = head.split(';').first();
        // Strip "item1." style group prefixes.
        const QByteArray name = prop.contains('.') ? prop.mid(prop.lastIndexOf('.') + 1) : prop;
        const QByteArray value = rawLine.mid(colon + 1).trimmed();
        if (name == "FN") {
            c.name = unescape(QString::fromUtf8(value));
        } else if (name == "N" && structuredName.isEmpty()) {
            const auto parts = QString::fromUtf8(value).split(QLatin1Char(';'));
            structuredName = (parts.value(1) + QLatin1Char(' ') + parts.value(0)).trimmed();
        } else if (name == "TEL") {
            QString number = QString::fromUtf8(value);
            if (number.startsWith(QLatin1String("tel:"), Qt::CaseInsensitive))
                number = number.mid(4);
            if (!number.isEmpty() && !c.numbers.contains(number))
                c.numbers << number;
        } else if (name == "EMAIL") {
            c.emails << QString::fromUtf8(value);
        } else if (name == "UID") {
            if (c.uid.isEmpty())
                c.uid = QString::fromUtf8(value);
        } else if (name == "PHOTO" && photo.isEmpty()) {
            if (value.startsWith("data:")) {
                photo = QByteArray::fromBase64(value.mid(value.indexOf(',') + 1));
            } else if (head.contains("ENCODING=B") || head.contains("ENCODING=BASE64")) {
                photo = QByteArray::fromBase64(value);
            }
        }
    }
    if (c.name.isEmpty())
        c.name = structuredName;
    if (c.name.isEmpty() && !c.numbers.isEmpty())
        c.name = c.numbers.first();
    if (!photo.isEmpty()) {
        const QString hint = photoKeyHint.isEmpty() ? (c.uid.isEmpty() ? QString::number(qHash(data)) : c.uid) : photoKeyHint;
        c.photoKey = QStringLiteral("contact/") + hint;
        ImageStore::instance()->insert(c.photoKey, photo);
    }
    return c;
}

QString ContactStore::normalize(const QString &address)
{
    if (address.contains(QLatin1Char('@')))
        return address.toLower();
    QString digits;
    digits.reserve(address.size());
    for (QChar ch : address)
        if (ch.isDigit())
            digits += ch;
    // Compare on the national significant part so "+1 503 555 0100" == "(503) 555-0100".
    return digits.size() > 10 ? digits.right(10) : digits;
}

QString ContactStore::prettyAddress(const QString &address)
{
    // RCS business chat agents look like "united_airlines_dsqopxgv_agent@rbm.goog".
    static const QRegularExpression rbm(QStringLiteral("^(.+?)(?:_[a-z0-9]{6,10})?_agent@rbm\\.goog$"));
    const auto m = rbm.match(address);
    if (m.hasMatch()) {
        QStringList words = m.captured(1).split(QLatin1Char('_'), Qt::SkipEmptyParts);
        for (QString &w : words)
            w[0] = w[0].toUpper();
        return words.join(QLatin1Char(' '));
    }
    static const QRegularExpression nanp(QStringLiteral("^\\+?1?(\\d{3})(\\d{3})(\\d{4})$"));
    QString compact = address;
    compact.remove(QRegularExpression(QStringLiteral("[\\s().-]")));
    const auto n = nanp.match(compact);
    if (n.hasMatch())
        return QStringLiteral("(%1) %2-%3").arg(n.captured(1), n.captured(2), n.captured(3));
    return address;
}

QString ContactStore::nameFor(const QString &address) const
{
    const auto it = m_byNumber.constFind(normalize(address));
    if (it != m_byNumber.cend())
        return m_contacts.at(*it).name;
    return prettyAddress(address);
}

QString ContactStore::avatarFor(const QString &address) const
{
    const auto it = m_byNumber.constFind(normalize(address));
    if (it != m_byNumber.cend() && !m_contacts.at(*it).photoKey.isEmpty())
        return ImageStore::urlFor(m_contacts.at(*it).photoKey);
    return {};
}

bool ContactStore::isKnown(const QString &address) const
{
    return m_byNumber.contains(normalize(address));
}

void ContactStore::setFilter(const QString &text)
{
    m_filtered.setFilterFixedString(text);
}

QString ContactStore::exportVCard(const QString &uid) const
{
    for (const Contact &c : m_contacts) {
        if (c.uid != uid)
            continue;
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/outgoing");
        QDir().mkpath(dir);
        QString base = c.name;
        base.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")), QStringLiteral("_"));
        const QString path = dir + QLatin1Char('/') + (base.isEmpty() ? QStringLiteral("contact") : base) + QStringLiteral(".vcf");
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return {};
        f.write(c.raw);
        return path;
    }
    return {};
}

int ContactStore::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_contacts.size());
}

QVariant ContactStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_contacts.size())
        return {};
    const Contact &c = m_contacts.at(index.row());
    switch (role) {
    case UidRole:
        return c.uid;
    case Qt::DisplayRole:
    case NameRole:
        return c.name;
    case NumbersRole:
        return c.numbers;
    case PrimaryNumberRole:
        return c.numbers.value(0, c.emails.value(0));
    case AvatarRole:
        return c.photoKey.isEmpty() ? QString() : ImageStore::urlFor(c.photoKey);
    case SearchRole:
        return c.name + QLatin1Char(' ') + c.numbers.join(QLatin1Char(' ')) + QLatin1Char(' ') + c.emails.join(QLatin1Char(' '));
    }
    return {};
}

QHash<int, QByteArray> ContactStore::roleNames() const
{
    return {{UidRole, "uid"}, {NameRole, "name"}, {NumbersRole, "numbers"}, {PrimaryNumberRole, "primaryNumber"}, {AvatarRole, "avatar"}};
}
