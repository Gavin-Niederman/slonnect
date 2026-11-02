#pragma once

#include "deviceplugin.h"

#include <QColor>
#include <QStringList>
#include <QTimer>

// Remote control of the phone's media players (kdeconnect_mprisremote).
class MediaPlugin : public DevicePlugin
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.media")
    Q_PROPERTY(QString player READ player WRITE setPlayer NOTIFY changed)
    Q_PROPERTY(QStringList players READ players NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QString albumArt READ albumArt NOTIFY changed)
    Q_PROPERTY(QColor artColor READ artColor NOTIFY artColorChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY changed)
    Q_PROPERTY(int length READ length NOTIFY changed)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY changed)
    // Position in ms at `positionTimestamp` (ms since epoch); QML extrapolates
    // while playing so we never poll the bus.
    Q_PROPERTY(int position READ position NOTIFY changed)
    Q_PROPERTY(double positionTimestamp READ positionTimestamp NOTIFY changed)
    Q_PROPERTY(bool hasMedia READ hasMedia NOTIFY changed)

public:
    explicit MediaPlugin(QObject *parent = nullptr);

    QString player() const { return m_player; }
    QStringList players() const { return m_players; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    QString albumArt() const { return m_albumArt; }
    QColor artColor() const { return m_artColor; }
    bool playing() const { return m_playing; }
    bool canSeek() const { return m_canSeek; }
    int length() const { return m_length; }
    int volume() const { return m_volume; }
    int position() const { return m_position; }
    double positionTimestamp() const { return m_positionTimestamp; }
    bool hasMedia() const { return !m_players.isEmpty(); }

    void setPlayer(const QString &player);
    void setVolume(int volume);

    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seekTo(int ms);
    Q_INVOKABLE void seekBy(int ms);

Q_SIGNALS:
    void changed();
    void artColorChanged();

protected:
    void attached() override;
    void detached() override;

private Q_SLOTS:
    void onPropertiesChanged();

private:
    void refresh();
    void updateArtColor();

    QTimer m_refreshTimer;
    QString m_player;
    QStringList m_players;
    QString m_title, m_artist, m_album, m_albumArt;
    QColor m_artColor;
    bool m_playing = false;
    bool m_canSeek = false;
    int m_length = 0;
    int m_volume = 0;
    int m_position = 0;
    double m_positionTimestamp = 0;
};
