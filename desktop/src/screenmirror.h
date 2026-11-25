#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QtQml/qqmlregistration.h>

// Mirrors the phone's screen in a desktop window via scrcpy over adb.
// Not a KDE Connect plugin: KDE Connect has no screen-mirroring API.
// Finds open TCP ports on one host by attempting connections, a bounded number
// at a time. Closed ports answer with a reset almost immediately, so a 20k-port
// range over a VPN takes seconds, not minutes.
class PortScanner : public QObject
{
    Q_OBJECT

public:
    PortScanner(const QString &host, quint16 first, quint16 last, QObject *parent = nullptr);
    void start();

Q_SIGNALS:
    void finished(const QList<quint16> &open);

private:
    void launchNext();
    void done(quint16 port, bool open);

    QString m_host;
    quint32 m_next;
    quint32 m_last;
    int m_inFlight = 0;
    QList<quint16> m_open;
};

class ScreenMirror : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.mirror")
    // "idle" | "connecting" | "running"
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool toolsAvailable READ toolsAvailable CONSTANT)
    Q_PROPERTY(bool screenOff READ screenOff WRITE setScreenOff NOTIFY screenOffChanged)

public:
    explicit ScreenMirror(QObject *parent = nullptr);
    ~ScreenMirror() override;

    QString state() const { return m_state; }
    bool toolsAvailable() const { return !m_adb.isEmpty() && !m_scrcpy.isEmpty(); }
    bool screenOff() const { return m_screenOff; }
    void setScreenOff(bool off);

    // `title` names the window; `knownAddresses` (e.g. from KDE Connect) help
    // pick the right phone when several adb devices are visible.
    Q_INVOKABLE void start(const QString &title, const QStringList &knownAddresses = {});
    Q_INVOKABLE void stop();

Q_SIGNALS:
    void stateChanged();
    void screenOffChanged();
    void failed(const QString &error);

private:
    using Done = std::function<void(int exitCode, const QString &out)>;
    void runAdb(const QStringList &args, Done done);
    void findDevice();
    void connectTo(const QString &target, std::function<void()> otherwise);
    void tryFixedPort(int index);
    void tryAdbMdns();
    void tryAvahi();
    void tryCachedEndpoint();
    void tryScan(int index);
    void tryCandidates(const QString &host, const QList<quint16> &ports, int i, std::function<void()> otherwise);
    void giveUp();

    // Android picks Wireless debugging's port at random from roughly this range.
    static constexpr quint16 ScanFirstPort = 30000;
    static constexpr quint16 ScanLastPort = 49999;
    static QStringList parseEndpoints(const QString &mdnsOutput);
    QString pickEndpoint(const QStringList &endpoints) const;
    void launch(const QString &serial);
    void setState(const QString &state);
    void fail(const QString &error);

    QString m_adb;
    QString m_scrcpy;
    QString m_state = QStringLiteral("idle");
    QString m_title;
    QStringList m_knownAddresses;
    bool m_screenOff = false;
    QPointer<QProcess> m_scrcpyProcess;
};
