#include "screenmirror.h"

#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>

ScreenMirror::ScreenMirror(QObject *parent)
    : QObject(parent)
    , m_adb(QStandardPaths::findExecutable(QStringLiteral("adb")))
    , m_scrcpy(QStandardPaths::findExecutable(QStringLiteral("scrcpy")))
    , m_screenOff(QSettings().value(QStringLiteral("mirror/screenOff"), false).toBool())
{
}

ScreenMirror::~ScreenMirror()
{
    stop();
}

void ScreenMirror::setScreenOff(bool off)
{
    if (off == m_screenOff)
        return;
    m_screenOff = off;
    QSettings().setValue(QStringLiteral("mirror/screenOff"), off);
    Q_EMIT screenOffChanged();
}

void ScreenMirror::setState(const QString &state)
{
    if (state != m_state) {
        m_state = state;
        Q_EMIT stateChanged();
    }
}

void ScreenMirror::fail(const QString &error)
{
    setState(QStringLiteral("idle"));
    Q_EMIT failed(error);
}

void ScreenMirror::start(const QString &title, const QStringList &knownAddresses)
{
    if (m_state != QLatin1String("idle"))
        return;
    if (!toolsAvailable())
        return fail(tr("Screen mirroring needs scrcpy and adb (android-tools) installed."));
    m_title = title;
    m_knownAddresses = knownAddresses;
    setState(QStringLiteral("connecting"));
    findDevice();
}

void ScreenMirror::stop()
{
    if (m_scrcpyProcess) {
        m_scrcpyProcess->terminate();
        if (!m_scrcpyProcess->waitForFinished(2000))
            m_scrcpyProcess->kill();
    }
    setState(QStringLiteral("idle"));
}

void ScreenMirror::runAdb(const QStringList &args, Done done)
{
    auto *p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::finished, this, [p, done](int code) {
        p->deleteLater();
        done(code, QString::fromLocal8Bit(p->readAll()));
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            fail(tr("Couldn't run adb."));
        }
    });
    p->start(m_adb, args);
}

void ScreenMirror::findDevice()
{
    // 1. Something already connected (USB, or a previous wireless session)?
    runAdb({QStringLiteral("devices")}, [this](int, const QString &out) {
        QStringList ready;
        bool unauthorized = false;
        for (const QString &line : out.split(QLatin1Char('\n'))) {
            const QStringList f = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (f.size() < 2 || f.first() == QLatin1String("List"))
                continue;
            if (f.at(1) == QLatin1String("device"))
                ready << f.first();
            else if (f.at(1) == QLatin1String("unauthorized"))
                unauthorized = true;
        }
        if (!ready.isEmpty()) {
            // Prefer the device whose address KDE Connect knows about.
            for (const QString &serial : std::as_const(ready))
                for (const QString &addr : std::as_const(m_knownAddresses))
                    if (serial.startsWith(addr + QLatin1Char(':')))
                        return launch(serial);
            return launch(ready.first());
        }
        if (unauthorized)
            return fail(tr("Allow USB debugging on the phone, then try again."));
        tryCachedEndpoint();
    });
}

void ScreenMirror::connectTo(const QString &target, std::function<void()> otherwise)
{
    runAdb({QStringLiteral("connect"), target}, [this, target, otherwise](int, const QString &out) {
        // "connected to x" or "already connected to x"
        if (out.contains(QLatin1String("connected to")) && !out.contains(QLatin1String("failed")))
            launch(target);
        else
            otherwise();
    });
}

void ScreenMirror::tryCachedEndpoint()
{
    // 2. Where we found the phone last time. Wireless debugging keeps its port until
    //    it is toggled or the phone reboots, so this is usually an instant hit.
    const QString last = QSettings().value(QStringLiteral("mirror/lastEndpoint")).toString();
    if (last.isEmpty())
        return tryFixedPort(0);
    connectTo(last, [this] { tryFixedPort(0); });
}

void ScreenMirror::tryFixedPort(int index)
{
    // 2. The address KDE Connect already uses, on adb's classic port. Enabled once
    //    per phone boot with `adb tcpip 5555`; works across VPNs where mDNS can't.
    if (index >= m_knownAddresses.size())
        return tryAdbMdns();
    connectTo(m_knownAddresses.at(index) + QStringLiteral(":5555"), [this, index] { tryFixedPort(index + 1); });
}

void ScreenMirror::tryAdbMdns()
{
    // 3. A phone paired for Wireless debugging announces its current (randomised)
    //    port over mDNS. Some adb builds (e.g. nixpkgs android-tools) lack mDNS.
    runAdb({QStringLiteral("mdns"), QStringLiteral("services")}, [this](int, const QString &out) {
        const QStringList endpoints = parseEndpoints(out);
        if (endpoints.isEmpty())
            return tryAvahi();
        connectTo(pickEndpoint(endpoints), [this] { tryAvahi(); });
    });
}

void ScreenMirror::tryAvahi()
{
    // 4. Same discovery through the system's Avahi daemon, for adb builds without mDNS.
    const QString avahi = QStandardPaths::findExecutable(QStringLiteral("avahi-browse"));
    if (avahi.isEmpty())
        return tryScan(0);
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p] {
        p->deleteLater();
        // Resolved lines: =;iface;proto;name;type;domain;host;address;port;txt
        QStringList endpoints;
        for (const QString &line : QString::fromLocal8Bit(p->readAllStandardOutput()).split(QLatin1Char('\n'))) {
            const QStringList f = line.split(QLatin1Char(';'));
            if (f.size() >= 9 && f.at(0) == QLatin1String("=") && f.at(2) == QLatin1String("IPv4"))
                endpoints << f.at(7) + QLatin1Char(':') + f.at(8);
        }
        if (endpoints.isEmpty())
            return tryScan(0);
        connectTo(pickEndpoint(endpoints), [this] { tryScan(0); });
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            tryScan(0);
        }
    });
    // -t: stop after the initial dump, so this returns within a second or two.
    p->start(avahi, {QStringLiteral("-rpt"), QStringLiteral("_adb-tls-connect._tcp")});
}

void ScreenMirror::tryScan(int index)
{
    // 5. Find Wireless debugging's random port by probing the phone's known
    //    addresses (e.g. its Tailscale IP, where mDNS can't reach). Only the real
    //    adb port accepts `adb connect`, since this computer is paired.
    if (index >= m_knownAddresses.size())
        return giveUp();
    const QString host = m_knownAddresses.at(index);
    auto *scanner = new PortScanner(host, ScanFirstPort, ScanLastPort, this);
    connect(scanner, &PortScanner::finished, this, [this, scanner, host, index](const QList<quint16> &open) {
        scanner->deleteLater();
        tryCandidates(host, open, 0, [this, index] { tryScan(index + 1); });
    });
    scanner->start();
}

void ScreenMirror::tryCandidates(const QString &host, const QList<quint16> &ports, int i, std::function<void()> otherwise)
{
    if (i >= ports.size())
        return otherwise();
    connectTo(host + QLatin1Char(':') + QString::number(ports.at(i)),
              [this, host, ports, i, otherwise] { tryCandidates(host, ports, i + 1, otherwise); });
}

void ScreenMirror::giveUp()
{
    fail(tr("Phone not found over adb. Turn on Wireless debugging on the phone (this computer must be paired once with "
            "\"adb pair\"), or connect USB with USB debugging on."));
}

QStringList ScreenMirror::parseEndpoints(const QString &mdnsOutput)
{
    static const QRegularExpression entry(QStringLiteral("_adb-tls-connect\\._tcp\\.?\\s+(\\S+:\\d+)"));
    QStringList endpoints;
    for (auto it = entry.globalMatch(mdnsOutput); it.hasNext();)
        endpoints << it.next().captured(1);
    return endpoints;
}

QString ScreenMirror::pickEndpoint(const QStringList &endpoints) const
{
    for (const QString &e : endpoints)
        for (const QString &addr : m_knownAddresses)
            if (e.startsWith(addr + QLatin1Char(':')))
                return e;
    return endpoints.first();
}

void ScreenMirror::launch(const QString &serial)
{
    auto *p = new QProcess(this);
    m_scrcpyProcess = p;
    p->setProcessChannelMode(QProcess::MergedChannels);
    QStringList args{QStringLiteral("--serial=") + serial, QStringLiteral("--window-title=") + m_title, QStringLiteral("--stay-awake")};
    if (m_screenOff)
        args << QStringLiteral("--turn-screen-off");
    connect(p, &QProcess::started, this, [this, serial] {
        setState(QStringLiteral("running"));
        // Remember network endpoints (host:port) for the fast path next time.
        static const QRegularExpression endpoint(QStringLiteral("^[^:\\s]+:\\d+$"));
        if (endpoint.match(serial).hasMatch())
            QSettings().setValue(QStringLiteral("mirror/lastEndpoint"), serial);
    });
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus status) {
        const QString out = QString::fromLocal8Bit(p->readAll());
        p->deleteLater();
        setState(QStringLiteral("idle"));
        // scrcpy exits 0 when its window is closed; 2 means the device went away.
        if (status == QProcess::NormalExit && code != 0 && code != 2) {
            const QString lastError = out.split(QLatin1Char('\n')).filter(QStringLiteral("ERROR")).value(0).trimmed();
            Q_EMIT failed(lastError.isEmpty() ? tr("scrcpy exited with code %1").arg(code) : lastError);
        }
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            fail(tr("Couldn't start scrcpy."));
        }
    });
    p->start(m_scrcpy, args);
}

static constexpr int ScanConcurrency = 256;
static constexpr int ScanTimeoutMs = 1500;

PortScanner::PortScanner(const QString &host, quint16 first, quint16 last, QObject *parent)
    : QObject(parent)
    , m_host(host)
    , m_next(first)
    , m_last(last)
{
}

void PortScanner::start()
{
    for (int i = 0; i < ScanConcurrency && m_next <= m_last; ++i)
        launchNext();
}

void PortScanner::launchNext()
{
    const auto port = quint16(m_next++);
    ++m_inFlight;
    auto *socket = new QTcpSocket(this);
    auto *timer = new QTimer(socket);
    timer->setSingleShot(true);
    // Each attempt settles exactly once: connected, refused/errored, or timed out.
    auto settle = [this, socket, port](bool open) {
        if (socket->property("settled").toBool())
            return;
        socket->setProperty("settled", true);
        socket->abort();
        socket->deleteLater();
        done(port, open);
    };
    connect(socket, &QTcpSocket::connected, this, [settle] { settle(true); });
    connect(socket, &QTcpSocket::errorOccurred, this, [settle] { settle(false); });
    connect(timer, &QTimer::timeout, this, [settle] { settle(false); });
    timer->start(ScanTimeoutMs);
    socket->connectToHost(m_host, port);
}

void PortScanner::done(quint16 port, bool open)
{
    --m_inFlight;
    if (open)
        m_open << port;
    if (m_next <= m_last)
        launchNext();
    else if (m_inFlight == 0)
        Q_EMIT finished(m_open);
}
