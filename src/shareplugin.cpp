#include "shareplugin.h"
#include "kdeconnect.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

static constexpr int MaxReceived = 100;

SharePlugin::SharePlugin(QObject *parent)
    : DevicePlugin(QStringLiteral("kdeconnect_share"), QStringLiteral("/share"), QStringLiteral("org.kde.kdeconnect.device.share"), parent)
{
}

void SharePlugin::attached()
{
    subscribe(QStringLiteral("shareReceived"), SLOT(onShareReceived(QString)));
    load();
}

QString SharePlugin::incomingFolder() const
{
    const QString fallback = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (deviceId().isEmpty())
        return fallback;
    const QString configPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kdeconnect/") + deviceId()
        + QStringLiteral("/kdeconnect_share/config");
    QSettings config(configPath, QSettings::IniFormat);
    QString path = config.value(QStringLiteral("General/incoming_path"), fallback).toString();
    if (path.startsWith(QLatin1String("file://")))
        path = QUrl(path).toLocalFile();
    return path;
}

int SharePlugin::shareUrls(const QVariantList &urls)
{
    QStringList list;
    for (const QVariant &v : urls) {
        QUrl url = v.metaType() == QMetaType::fromType<QUrl>() ? v.toUrl() : QUrl::fromUserInput(v.toString());
        if (url.isValid() && (!url.isLocalFile() || QFileInfo(url.toLocalFile()).isFile()))
            list << url.toString();
    }
    if (!list.isEmpty())
        invoke(QStringLiteral("shareUrls"), {list});
    return int(list.size());
}

void SharePlugin::shareText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;
    const QUrl url(trimmed, QUrl::StrictMode);
    // Links open on the phone; anything else lands as text.
    if (url.isValid() && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https")) && !trimmed.contains(QLatin1Char(' ')))
        invoke(QStringLiteral("shareUrl"), {trimmed});
    else
        invoke(QStringLiteral("shareText"), {text});
}

void SharePlugin::onShareReceived(const QString &url)
{
    const QUrl u(url);
    QVariantMap entry;
    entry[QStringLiteral("url")] = url;
    entry[QStringLiteral("isFile")] = u.isLocalFile();
    entry[QStringLiteral("name")] = u.isLocalFile() ? u.fileName() : url;
    entry[QStringLiteral("size")] = u.isLocalFile() ? QFileInfo(u.toLocalFile()).size() : 0;
    entry[QStringLiteral("time")] = QDateTime::currentMSecsSinceEpoch();
    m_received.prepend(entry);
    while (m_received.size() > MaxReceived)
        m_received.removeLast();
    save();
    Q_EMIT receivedChanged();
    Q_EMIT fileReceived(entry[QStringLiteral("name")].toString());
}

void SharePlugin::openIncomingFolder() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(incomingFolder()));
}

void SharePlugin::openReceived(const QString &url) const
{
    QDesktopServices::openUrl(QUrl(url));
}

void SharePlugin::revealReceived(const QString &url) const
{
    const QUrl u(url);
    if (u.isLocalFile())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(u.toLocalFile()).absolutePath()));
}

void SharePlugin::clearReceived()
{
    m_received.clear();
    save();
    Q_EMIT receivedChanged();
}

void SharePlugin::load()
{
    m_received = QSettings().value(QStringLiteral("share/") + deviceId() + QStringLiteral("/received")).toList();
    Q_EMIT receivedChanged();
}

void SharePlugin::save() const
{
    QSettings().setValue(QStringLiteral("share/") + deviceId() + QStringLiteral("/received"), m_received);
}
