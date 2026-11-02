#include "mediaplugin.h"
#include "kdeconnect.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QImage>
#include <QUrl>
#include <QtConcurrent>

MediaPlugin::MediaPlugin(QObject *parent)
    : DevicePlugin(QStringLiteral("kdeconnect_mprisremote"), QStringLiteral("/mprisremote"), QStringLiteral("org.kde.kdeconnect.device.mprisremote"), parent)
{
    // propertiesChanged fires in bursts; coalesce into one GetAll.
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(40);
    connect(&m_refreshTimer, &QTimer::timeout, this, &MediaPlugin::refresh);
}

void MediaPlugin::attached()
{
    subscribe(QStringLiteral("propertiesChanged"), SLOT(onPropertiesChanged()));
    invoke(QStringLiteral("requestPlayerList"));
    refresh();
}

void MediaPlugin::detached()
{
    m_players.clear();
    m_title.clear();
    m_artist.clear();
    m_album.clear();
    m_albumArt.clear();
    m_playing = false;
    Q_EMIT changed();
}

void MediaPlugin::onPropertiesChanged()
{
    m_refreshTimer.start();
}

void MediaPlugin::refresh()
{
    if (!available())
        return;
    kdc::getAll(path(), iface(), this, [this](const QVariantMap &p) {
        m_player = p.value(QStringLiteral("player")).toString();
        m_players = p.value(QStringLiteral("playerList")).toStringList();
        m_title = p.value(QStringLiteral("title")).toString();
        m_artist = p.value(QStringLiteral("artist")).toString();
        m_album = p.value(QStringLiteral("album")).toString();
        m_playing = p.value(QStringLiteral("isPlaying")).toBool();
        m_canSeek = p.value(QStringLiteral("canSeek")).toBool();
        m_length = p.value(QStringLiteral("length")).toInt();
        m_volume = p.value(QStringLiteral("volume")).toInt();
        m_position = p.value(QStringLiteral("position")).toInt();
        m_positionTimestamp = double(QDateTime::currentMSecsSinceEpoch());
        const QString art = p.value(QStringLiteral("localAlbumArtUrl")).toString();
        const bool artChanged = art != m_albumArt;
        m_albumArt = art;
        Q_EMIT changed();
        if (artChanged)
            updateArtColor();
    });
}

void MediaPlugin::updateArtColor()
{
    const QString file = QUrl(m_albumArt).toLocalFile();
    if (file.isEmpty()) {
        m_artColor = QColor();
        Q_EMIT artColorChanged();
        return;
    }
    auto *watcher = new QFutureWatcher<QColor>(this);
    connect(watcher, &QFutureWatcher<QColor>::finished, this, [this, watcher] {
        watcher->deleteLater();
        m_artColor = watcher->result();
        Q_EMIT artColorChanged();
    });
    // Seed colour for the "Material You" style tint: the most chromatic of the
    // average colours in a coarse grid over the cover.
    watcher->setFuture(QtConcurrent::run([file] {
        const QImage img = QImage(file).scaled(6, 6, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QColor best;
        double bestScore = -1;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const QColor c = img.pixelColor(x, y).toHsl();
                const double l = c.lightnessF();
                const double score = c.hslSaturationF() * (1.0 - std::abs(l - 0.5) * 1.6);
                if (score > bestScore) {
                    bestScore = score;
                    best = c;
                }
            }
        }
        return best.isValid() ? best.toRgb() : QColor();
    }));
}

void MediaPlugin::setPlayer(const QString &player)
{
    if (player == m_player)
        return;
    m_player = player;
    kdc::setProperty(path(), iface(), QStringLiteral("player"), player);
    m_refreshTimer.start();
}

void MediaPlugin::setVolume(int volume)
{
    volume = qBound(0, volume, 100);
    if (volume == m_volume)
        return;
    m_volume = volume;
    kdc::setProperty(path(), iface(), QStringLiteral("volume"), volume);
    Q_EMIT changed();
}

void MediaPlugin::playPause()
{
    // Optimistic flip so the button reacts instantly; the phone confirms shortly after.
    m_position = m_position + (m_playing ? int(QDateTime::currentMSecsSinceEpoch() - m_positionTimestamp) : 0);
    m_positionTimestamp = double(QDateTime::currentMSecsSinceEpoch());
    m_playing = !m_playing;
    Q_EMIT changed();
    invoke(QStringLiteral("sendAction"), {QStringLiteral("PlayPause")});
}

void MediaPlugin::next()
{
    invoke(QStringLiteral("sendAction"), {QStringLiteral("Next")});
}

void MediaPlugin::previous()
{
    invoke(QStringLiteral("sendAction"), {QStringLiteral("Previous")});
}

void MediaPlugin::seekTo(int ms)
{
    if (!m_canSeek)
        return;
    m_position = qBound(0, ms, m_length);
    m_positionTimestamp = double(QDateTime::currentMSecsSinceEpoch());
    kdc::setProperty(path(), iface(), QStringLiteral("position"), m_position);
    Q_EMIT changed();
}

void MediaPlugin::seekBy(int ms)
{
    const int now = m_position + (m_playing ? int(QDateTime::currentMSecsSinceEpoch() - m_positionTimestamp) : 0);
    seekTo(now + ms);
}
