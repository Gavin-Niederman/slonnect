// Desktop hardware nodes: volume, brightness, Bluetooth.
#include "nodes_p.h"
#include "actionrun.h"

#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <optional>

namespace nodes::detail {

void runTool(const Ctx &c, const QString &program, const QStringList &args, std::function<void(const QString &)> onDone, int timeoutMs)
{
    auto *p = new QProcess(c.run.data());
    auto *timer = new QTimer(p);
    timer->setSingleShot(true);
    const QString name = QFileInfo(program).fileName();
    QObject::connect(timer, &QTimer::timeout, p, [c, p, name] {
        p->disconnect();
        p->kill();
        p->deleteLater();
        c.fail(QObject::tr("%1 didn't finish in time").arg(name));
    });
    QObject::connect(p, &QProcess::errorOccurred, p, [c, p, name](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->disconnect();
        p->deleteLater();
        c.fail(QObject::tr("Couldn't start %1").arg(name));
    });
    QObject::connect(p, &QProcess::finished, p, [c, p, name, onDone](int code, QProcess::ExitStatus status) {
        p->disconnect();
        p->deleteLater();
        const QString out = QString::fromLocal8Bit(p->readAllStandardOutput());
        if (status != QProcess::NormalExit || code != 0) {
            const QString err = QString::fromLocal8Bit(p->readAllStandardError()).trimmed();
            return c.fail(err.isEmpty() ? QObject::tr("%1 exited with %2").arg(name).arg(code) : err.left(300));
        }
        onDone(out);
    });
    p->start(program, args);
    timer->start(timeoutMs);
}

BluezState parseBluez(const QVariant &managedObjects)
{
    BluezState s;
    const QVariantMap objects = plain(managedObjects).toMap();
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        const QVariantMap ifaces = it.value().toMap();
        if (const auto a = ifaces.find(QStringLiteral("org.bluez.Adapter1")); a != ifaces.end())
            s.adapters.insert(it.key(), a->toMap().value(QStringLiteral("Powered")).toBool());
        if (const auto d = ifaces.find(QStringLiteral("org.bluez.Device1")); d != ifaces.end()) {
            const QVariantMap p = d->toMap();
            const QString alias = p.value(QStringLiteral("Alias")).toString();
            s.devices << BluezDevice{it.key(),
                                     alias.isEmpty() ? p.value(QStringLiteral("Name")).toString() : alias,
                                     p.value(QStringLiteral("Address")).toString(),
                                     p.value(QStringLiteral("Adapter")).toString(),
                                     p.value(QStringLiteral("Connected")).toBool(),
                                     p.value(QStringLiteral("Paired")).toBool()};
        }
    }
    // Paired devices first, then by name, so matching and suggestions prefer them.
    std::stable_sort(s.devices.begin(), s.devices.end(), [](const BluezDevice &a, const BluezDevice &b) {
        return a.paired != b.paired ? a.paired : a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return s;
}

namespace {

// --- Volume -----------------------------------------------------------------------

void volume(const Ctx &c)
{
    const bool mic = c.str("device") == QLatin1String("microphone");
    const QString action = c.str("action");
    const int pct = std::clamp(int(std::lround(c.num("percent"))), 0, 150);

    if (const QString wpctl = QStandardPaths::findExecutable(QStringLiteral("wpctl")); !wpctl.isEmpty()) {
        const QString target = mic ? QStringLiteral("@DEFAULT_AUDIO_SOURCE@") : QStringLiteral("@DEFAULT_AUDIO_SINK@");
        QStringList args;
        if (action == QLatin1String("set"))
            args = {QStringLiteral("set-volume"), target, QStringLiteral("%1%").arg(pct)};
        else if (action == QLatin1String("up"))
            args = {QStringLiteral("set-volume"), QStringLiteral("-l"), QStringLiteral("1.0"), target, QStringLiteral("%1%+").arg(pct)};
        else if (action == QLatin1String("down"))
            args = {QStringLiteral("set-volume"), target, QStringLiteral("%1%-").arg(pct)};
        else if (action == QLatin1String("mute"))
            args = {QStringLiteral("set-mute"), target, QStringLiteral("1")};
        else if (action == QLatin1String("unmute"))
            args = {QStringLiteral("set-mute"), target, QStringLiteral("0")};
        else if (action == QLatin1String("toggle mute"))
            args = {QStringLiteral("set-mute"), target, QStringLiteral("toggle")};
        auto report = [c, wpctl, target] {
            runTool(c, wpctl, {QStringLiteral("get-volume"), target}, [c](const QString &out) {
                // "Volume: 0.46 [MUTED]"
                static const QRegularExpression re(QStringLiteral("Volume:\\s*([0-9.]+)"));
                const auto m = re.match(out);
                c.next({{QStringLiteral("volume"), m.hasMatch() ? int(std::lround(m.captured(1).toDouble() * 100)) : -1},
                        {QStringLiteral("muted"), out.contains(QLatin1String("MUTED"))}});
            });
        };
        if (args.isEmpty())
            return report();
        return runTool(c, wpctl, args, [report](const QString &) { report(); });
    }

    if (const QString pactl = QStandardPaths::findExecutable(QStringLiteral("pactl")); !pactl.isEmpty()) {
        const QString kind = mic ? QStringLiteral("source") : QStringLiteral("sink");
        const QString target = mic ? QStringLiteral("@DEFAULT_SOURCE@") : QStringLiteral("@DEFAULT_SINK@");
        const QString setVolume = QStringLiteral("set-%1-volume").arg(kind), setMute = QStringLiteral("set-%1-mute").arg(kind);
        QStringList args;
        if (action == QLatin1String("set"))
            args = {setVolume, target, QStringLiteral("%1%").arg(pct)};
        else if (action == QLatin1String("up"))
            args = {setVolume, target, QStringLiteral("+%1%").arg(pct)};
        else if (action == QLatin1String("down"))
            args = {setVolume, target, QStringLiteral("-%1%").arg(pct)};
        else if (action == QLatin1String("mute"))
            args = {setMute, target, QStringLiteral("1")};
        else if (action == QLatin1String("unmute"))
            args = {setMute, target, QStringLiteral("0")};
        else if (action == QLatin1String("toggle mute"))
            args = {setMute, target, QStringLiteral("toggle")};
        auto report = [c, pactl, kind, target] {
            runTool(c, pactl, {QStringLiteral("get-%1-volume").arg(kind), target}, [c, pactl, kind, target](const QString &vol) {
                static const QRegularExpression re(QStringLiteral("(\\d+)%"));
                const auto m = re.match(vol);
                const int level = m.hasMatch() ? m.captured(1).toInt() : -1;
                runTool(c, pactl, {QStringLiteral("get-%1-mute").arg(kind), target}, [c, level](const QString &mute) {
                    c.next({{QStringLiteral("volume"), level}, {QStringLiteral("muted"), mute.contains(QLatin1String("yes"))}});
                });
            });
        };
        if (args.isEmpty())
            return report();
        return runTool(c, pactl, args, [report](const QString &) { report(); });
    }

    c.fail(QObject::tr("Needs wpctl (PipeWire) or pactl (PulseAudio)"));
}

// --- Brightness -------------------------------------------------------------------

struct Backlight {
    QString subsystem; // "backlight" | "leds"
    QString name;
    int current = 0;
    int max = 0;
};

int readInt(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()).trimmed().toInt() : -1;
}

std::optional<Backlight> findBacklight(bool keyboard)
{
    const QString subsystem = keyboard ? QStringLiteral("leds") : QStringLiteral("backlight");
    const QString root = QStringLiteral("/sys/class/") + subsystem;
    QStringList names = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System);
    if (keyboard) {
        names.removeIf([](const QString &n) { return !n.contains(QLatin1String("kbd_backlight")); });
    } else {
        // Firmware interfaces know the panel's real range best, then platform, then raw.
        auto rank = [&](const QString &n) {
            QFile f(root + QLatin1Char('/') + n + QStringLiteral("/type"));
            const QString type = f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()).trimmed() : QString();
            return type == QLatin1String("firmware") ? 0 : type == QLatin1String("platform") ? 1 : 2;
        };
        std::stable_sort(names.begin(), names.end(), [&](const QString &a, const QString &b) { return rank(a) < rank(b); });
    }
    for (const QString &n : std::as_const(names)) {
        const QString dir = root + QLatin1Char('/') + n;
        const int max = readInt(dir + QStringLiteral("/max_brightness"));
        if (max > 0)
            return Backlight{subsystem, n, readInt(dir + QStringLiteral("/brightness")), max};
    }
    return std::nullopt;
}

int targetLevel(const QString &action, int pct, int current, int max, int floor)
{
    const int step = int(std::lround(pct * max / 100.0));
    if (action == QLatin1String("set"))
        return std::clamp(step, 0, max);
    if (action == QLatin1String("up"))
        return std::clamp(current + std::max(1, step), floor, max);
    if (action == QLatin1String("down"))
        return std::clamp(current - std::max(1, step), floor, max);
    return current;
}

// External monitors over DDC/CI (VCP feature 0x10 is brightness).
void ddcBrightness(const Ctx &c, const QString &ddcutil, const QString &action, int pct)
{
    runTool(
        c, ddcutil, {QStringLiteral("getvcp"), QStringLiteral("10"), QStringLiteral("--brief")},
        [c, ddcutil, action, pct](const QString &out) {
            // "VCP 10 C 50 100"
            const QStringList f = out.simplified().split(QLatin1Char(' '));
            if (f.size() < 5)
                return c.fail(QObject::tr("Unexpected ddcutil output: %1").arg(out.trimmed()));
            const int cur = f.at(3).toInt(), max = std::max(1, f.at(4).toInt());
            const int level = targetLevel(action, pct, cur, max, 1);
            if (level == cur || action == QLatin1String("get"))
                return c.next({{QStringLiteral("brightness"), int(std::lround(cur * 100.0 / max))}});
            runTool(
                c, ddcutil, {QStringLiteral("setvcp"), QStringLiteral("10"), QString::number(level)},
                [c, level, max](const QString &) { c.next({{QStringLiteral("brightness"), int(std::lround(level * 100.0 / max))}}); }, 30000);
        },
        30000);
}

void brightness(const Ctx &c)
{
    const bool keyboard = c.str("target") == QLatin1String("keyboard");
    const QString action = c.str("action");
    const int pct = std::clamp(int(std::lround(c.num("percent"))), 0, 100);

    const auto light = findBacklight(keyboard);
    if (!light) {
        if (const QString ddcutil = QStandardPaths::findExecutable(QStringLiteral("ddcutil")); !keyboard && !ddcutil.isEmpty())
            return ddcBrightness(c, ddcutil, action, pct);
        return c.fail(keyboard ? QObject::tr("No keyboard backlight found") : QObject::tr("No screen backlight found (external monitors need ddcutil)"));
    }

    // Never turn the screen fully off by stepping down; "set 0" still can.
    const int level = targetLevel(action, pct, light->current, light->max, keyboard ? 0 : 1);
    const QVariantMap out{{QStringLiteral("brightness"), int(std::lround(level * 100.0 / light->max))}};
    if (level == light->current)
        return c.next(out);

    // Writable directly (udev rule / group)? Otherwise logind lets the active session do it.
    QFile f(QStringLiteral("/sys/class/%1/%2/brightness").arg(light->subsystem, light->name));
    if (f.open(QIODevice::WriteOnly) && f.write(QByteArray::number(level)) > 0) {
        f.close();
        return c.next(out);
    }
    dbusCall(c, QDBusConnection::systemBus(), QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1/session/auto"),
             QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("SetBrightness"), {light->subsystem, light->name, uint(level)},
             [c, out](const QDBusMessage &) { c.next(out); });
}

// --- Bluetooth --------------------------------------------------------------------

const BluezDevice *matchDevice(const BluezState &s, const QString &query)
{
    const QString q = query.trimmed();
    if (q.isEmpty())
        return nullptr;
    for (const auto &d : s.devices)
        if (d.name.compare(q, Qt::CaseInsensitive) == 0 || d.address.compare(q, Qt::CaseInsensitive) == 0)
            return &d;
    for (const auto &d : s.devices)
        if (d.name.contains(q, Qt::CaseInsensitive))
            return &d;
    return nullptr;
}

void bluetooth(const Ctx &c)
{
    const QString action = c.str("action");
    const QString query = c.str("device");
    const auto bus = QDBusConnection::systemBus();
    const QString service = QStringLiteral("org.bluez");

    auto setPowered = [c, bus, service](const QString &adapter, bool on, std::function<void()> then) {
        dbusCall(c, bus, service, adapter, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"),
                 {QStringLiteral("org.bluez.Adapter1"), QStringLiteral("Powered"), QVariant::fromValue(QDBusVariant(on))},
                 [then](const QDBusMessage &) { then(); });
    };

    dbusCall(
        c, bus, service, QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("GetManagedObjects"), {},
        [=](const QDBusMessage &reply) {
            const BluezState s = parseBluez(reply.arguments().value(0));
            if (s.adapters.isEmpty())
                return c.fail(QObject::tr("No Bluetooth adapter"));
            const BluezDevice *found = matchDevice(s, query);
            if (!query.trimmed().isEmpty() && !found)
                return c.fail(QObject::tr("No Bluetooth device matching \"%1\"").arg(query.trimmed()));
            const BluezDevice dev = found ? *found : BluezDevice{};
            const QString adapter = found && s.adapters.contains(dev.adapter) ? dev.adapter : s.adapters.firstKey();
            const bool powered = s.adapters.value(adapter);

            auto finish = [c, dev](bool poweredNow, bool connectedNow) {
                c.next({{QStringLiteral("powered"), poweredNow}, {QStringLiteral("connected"), connectedNow}, {QStringLiteral("device"), dev.name}});
            };

            if (action == QLatin1String("status"))
                return finish(powered, dev.connected);
            if (action.endsWith(QLatin1String("power"))) {
                const bool on = action == QLatin1String("power on") ? true : action == QLatin1String("power off") ? false : !powered;
                return setPowered(adapter, on, [finish, on, dev] { finish(on, on && dev.connected); });
            }

            if (dev.path.isEmpty())
                return c.fail(QObject::tr("Pick a device to %1").arg(action));
            const bool connect = action == QLatin1String("connect") || (action == QLatin1String("toggle connection") && !dev.connected);
            const QString method = connect ? QStringLiteral("Connect") : QStringLiteral("Disconnect");
            auto run = [c, bus, service, dev, method, connect, finish] {
                // Connecting can take a while: the device may need to wake up.
                dbusCall(c, bus, service, dev.path, QStringLiteral("org.bluez.Device1"), method, {},
                         [finish, connect](const QDBusMessage &) { finish(true, connect); }, 30000);
            };
            if (connect && !powered)
                return setPowered(adapter, true, run);
            run();
        });
}

} // namespace

void addSystemNodes(QList<NodeType> &t)
{
    const QString desktop = QObject::tr("Desktop");

    t << NodeType{QStringLiteral("desktop.volume"), QObject::tr("Volume"), desktop, QStringLiteral("volume_up"),
                  QObject::tr("Sets or reads this computer's speaker or microphone volume (PipeWire or PulseAudio). "
                              "Percent is the level for set and the step for up/down."),
                  {flowIn(), choice(QStringLiteral("device"), QObject::tr("Device"), {QStringLiteral("speaker"), QStringLiteral("microphone")}),
                   choice(QStringLiteral("action"), QObject::tr("Action"),
                          {QStringLiteral("set"), QStringLiteral("up"), QStringLiteral("down"), QStringLiteral("mute"), QStringLiteral("unmute"),
                           QStringLiteral("toggle mute"), QStringLiteral("get")}),
                   number(QStringLiteral("percent"), QObject::tr("Percent"), 50)},
                  {flowOut(), number(QStringLiteral("volume"), QObject::tr("Volume %")), boolean(QStringLiteral("muted"), QObject::tr("Muted"))},
                  volume,
                  {},
                  250};

    t << NodeType{QStringLiteral("desktop.brightness"), QObject::tr("Brightness"), desktop, QStringLiteral("brightness_6"),
                  QObject::tr("Sets or reads the screen or keyboard backlight. Percent is the level for set and the step for up/down. "
                              "External monitors work through ddcutil when it's installed."),
                  {flowIn(), choice(QStringLiteral("target"), QObject::tr("Light"), {QStringLiteral("screen"), QStringLiteral("keyboard")}),
                   choice(QStringLiteral("action"), QObject::tr("Action"), {QStringLiteral("set"), QStringLiteral("up"), QStringLiteral("down"), QStringLiteral("get")}),
                   number(QStringLiteral("percent"), QObject::tr("Percent"), 50)},
                  {flowOut(), number(QStringLiteral("brightness"), QObject::tr("Level %"))},
                  brightness,
                  {},
                  240};

    t << NodeType{QStringLiteral("desktop.bluetooth"), QObject::tr("Bluetooth"), desktop, QStringLiteral("bluetooth"),
                  QObject::tr("Turns Bluetooth on or off, or connects a paired device (headphones, speaker, …). Connecting powers the adapter on first."),
                  {flowIn(),
                   choice(QStringLiteral("action"), QObject::tr("Action"),
                          {QStringLiteral("connect"), QStringLiteral("disconnect"), QStringLiteral("toggle connection"), QStringLiteral("power on"),
                           QStringLiteral("power off"), QStringLiteral("toggle power"), QStringLiteral("status")}),
                   completing(text(QStringLiteral("device"), QObject::tr("Device"), {}, QObject::tr("name or address")), QStringLiteral("bluetooth.device"))},
                  {flowOut(), boolean(QStringLiteral("powered"), QObject::tr("Powered")), boolean(QStringLiteral("connected"), QObject::tr("Connected")),
                   text(QStringLiteral("device"), QObject::tr("Device"))},
                  bluetooth,
                  {},
                  270};
}

} // namespace nodes::detail
