#include "nodetypes.h"
#include "actionrun.h"
#include "kdeconnect.h"
#include "messagetypes.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusSignature>
#include <QDBusVariant>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <memory>

namespace nodes {

bool Ctx::flag(const char *id) const
{
    const QVariant v = in.value(QLatin1String(id));
    if (v.metaType() == QMetaType::fromType<QString>()) {
        const QString s = v.toString().trimmed().toLower();
        return !(s.isEmpty() || s == QLatin1String("false") || s == QLatin1String("0") || s == QLatin1String("no"));
    }
    return v.toBool();
}

QString Ctx::deviceId() const
{
    return run ? run->deviceId() : QString();
}

QString Ctx::devicePath(const QString &suffix) const
{
    return kdc::devicePath(deviceId()) + suffix;
}

void Ctx::log(const QString &message) const
{
    if (run)
        run->log(nodeUid, message);
}

const PinDef *NodeType::input(const QString &id) const
{
    for (const auto &p : inputs)
        if (p.id == id)
            return &p;
    return nullptr;
}

const PinDef *NodeType::output(const QString &id) const
{
    for (const auto &p : outputs)
        if (p.id == id)
            return &p;
    return nullptr;
}

QString kindName(PinKind kind)
{
    switch (kind) {
    case PinKind::Flow:
        return QStringLiteral("flow");
    case PinKind::Text:
        return QStringLiteral("text");
    case PinKind::Number:
        return QStringLiteral("number");
    case PinKind::Bool:
        return QStringLiteral("bool");
    case PinKind::Choice:
        return QStringLiteral("choice");
    }
    return {};
}

namespace {

using Reply = std::function<void(const QDBusMessage &)>;

// D-Bus value -> plain QVariant (lists, maps, strings, numbers).
QVariant plain(const QVariant &v);

QVariant demarshal(const QDBusArgument &arg)
{
    switch (arg.currentType()) {
    case QDBusArgument::BasicType:
    case QDBusArgument::VariantType:
        return plain(arg.asVariant());
    case QDBusArgument::ArrayType: {
        QVariantList list;
        arg.beginArray();
        while (!arg.atEnd())
            list << demarshal(arg);
        arg.endArray();
        return list;
    }
    case QDBusArgument::StructureType: {
        QVariantList list;
        arg.beginStructure();
        while (!arg.atEnd())
            list << demarshal(arg);
        arg.endStructure();
        return list;
    }
    case QDBusArgument::MapType: {
        QVariantMap map;
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            const QString key = demarshal(arg).toString();
            map.insert(key, demarshal(arg));
            arg.endMapEntry();
        }
        arg.endMap();
        return map;
    }
    default:
        return {};
    }
}

QVariant plain(const QVariant &v)
{
    if (v.metaType() == QMetaType::fromType<QDBusVariant>())
        return plain(v.value<QDBusVariant>().variant());
    if (v.metaType() == QMetaType::fromType<QDBusArgument>())
        return demarshal(v.value<QDBusArgument>());
    if (v.metaType() == QMetaType::fromType<QDBusObjectPath>())
        return v.value<QDBusObjectPath>().path();
    if (v.metaType() == QMetaType::fromType<QDBusSignature>())
        return v.value<QDBusSignature>().signature();
    return v;
}

QString toText(const QVariant &v)
{
    const QVariant p = plain(v);
    if (p.metaType() == QMetaType::fromType<QVariantList>() || p.metaType() == QMetaType::fromType<QVariantMap>()
        || p.metaType() == QMetaType::fromType<QStringList>()) {
        const QJsonValue j = QJsonValue::fromVariant(p);
        return QString::fromUtf8(j.isArray() ? QJsonDocument(j.toArray()).toJson(QJsonDocument::Compact)
                                             : QJsonDocument(j.toObject()).toJson(QJsonDocument::Compact));
    }
    return p.toString();
}

void dbusCall(const Ctx &c, const QDBusConnection &bus, const QString &service, const QString &path, const QString &iface, const QString &method,
              const QVariantList &args, Reply onReply, int timeoutMs = -1)
{
    auto msg = QDBusMessage::createMethodCall(service, path, iface, method);
    msg.setArguments(args);
    auto *w = new QDBusPendingCallWatcher(bus.asyncCall(msg, timeoutMs), c.run.data());
    QObject::connect(w, &QDBusPendingCallWatcher::finished, w, [c, onReply](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() == QDBusMessage::ErrorMessage)
            return c.fail(reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage());
        onReply(reply);
    });
}

// Call a method on one of the triggering device's KDE Connect plugins.
void phoneCall(const Ctx &c, const QString &suffix, const QString &iface, const QString &method, const QVariantList &args = {},
               Reply onReply = {})
{
    if (c.deviceId().isEmpty())
        return c.fail(QObject::tr("No phone to talk to"));
    dbusCall(c, kdc::bus(), kdc::Service, c.devicePath(suffix), iface, method, args, onReply ? onReply : Reply([c](const QDBusMessage &) {
        c.next();
    }));
}

QString expandHome(QString s)
{
    s = s.trimmed();
    if (s == QLatin1String("~") || s.startsWith(QLatin1String("~/")))
        s.replace(0, 1, QDir::homePath());
    return s;
}

// "Play/Pause" -> "PlayPause": choice labels double as MPRIS method names.
QString methodName(const QString &label)
{
    QString s = label;
    return s.remove(QLatin1Char('/')).remove(QLatin1Char(' '));
}

QVariant fromJson(const QJsonValue &v)
{
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (std::floor(d) == d && std::abs(d) <= 2147483647.0)
            return int(d);
        return d;
    }
    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        bool allStrings = !a.isEmpty();
        QVariantList list;
        for (const auto &e : a) {
            allStrings = allStrings && e.isString();
            list << fromJson(e);
        }
        if (allStrings) {
            QStringList strings;
            for (const auto &e : a)
                strings << e.toString();
            return strings;
        }
        return list;
    }
    if (v.isObject()) {
        QVariantMap map;
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            map.insert(it.key(), QVariant::fromValue(QDBusVariant(fromJson(it.value()))));
        return map;
    }
    return v.toVariant();
}

bool compare(const QVariant &a, const QString &op, const QVariant &b)
{
    const QString sa = toText(a), sb = toText(b);
    bool na = false, nb = false;
    const double da = sa.trimmed().toDouble(&na), db = sb.trimmed().toDouble(&nb);
    const bool numeric = na && nb;
    if (op == QLatin1String("="))
        return numeric ? da == db : sa == sb;
    if (op == QStringLiteral("≠"))
        return numeric ? da != db : sa != sb;
    if (op == QLatin1String("contains"))
        return sa.contains(sb, Qt::CaseInsensitive);
    if (op == QLatin1String(">"))
        return numeric ? da > db : sa.compare(sb) > 0;
    if (op == QLatin1String("<"))
        return numeric ? da < db : sa.compare(sb) < 0;
    if (op == QLatin1String("is empty"))
        return sa.trimmed().isEmpty();
    if (op == QLatin1String("is not empty"))
        return !sa.trimmed().isEmpty();
    return false;
}

PinDef flowIn()
{
    return {QStringLiteral("in"), QString(), PinKind::Flow};
}

PinDef flowOut(const QString &id = QStringLiteral("out"), const QString &label = {})
{
    return {id, label, PinKind::Flow};
}

PinDef text(const QString &id, const QString &label, const QString &def = {}, const QString &placeholder = {})
{
    return {id, label, PinKind::Text, def, {}, placeholder};
}

PinDef number(const QString &id, const QString &label, double def = 0)
{
    return {id, label, PinKind::Number, def};
}

PinDef choice(const QString &id, const QString &label, const QStringList &options)
{
    return {id, label, PinKind::Choice, options.value(0), options};
}

// --- Desktop: MPRIS ---------------------------------------------------------------

void desktopMedia(const Ctx &c)
{
    const QString method = methodName(c.str("action"));
    const QString wanted = c.str("player").trimmed();
    auto bus = QDBusConnection::sessionBus();
    dbusCall(c, bus, QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
             QStringLiteral("ListNames"), {}, [c, method, wanted, bus](const QDBusMessage &reply) {
                 QStringList players;
                 for (const QString &name : reply.arguments().value(0).toStringList()) {
                     if (!name.startsWith(QLatin1String("org.mpris.MediaPlayer2.")))
                         continue;
                     // KDE Connect mirrors the phone's players here; they have their own node.
                     if (wanted.isEmpty() && name.contains(QLatin1String("kdeconnect")))
                         continue;
                     if (!wanted.isEmpty() && !name.contains(wanted, Qt::CaseInsensitive))
                         continue;
                     players << name;
                 }
                 if (players.isEmpty())
                     return c.fail(wanted.isEmpty() ? QObject::tr("No media player is running") : QObject::tr("No player matching \"%1\"").arg(wanted));

                 auto send = [c, method, bus](const QString &player) {
                     dbusCall(c, bus, player, QStringLiteral("/org/mpris/MediaPlayer2"), QStringLiteral("org.mpris.MediaPlayer2.Player"), method, {},
                              [c, player](const QDBusMessage &) { c.next({{QStringLiteral("player"), player.mid(23)}}); });
                 };
                 if (players.size() == 1)
                     return send(players.first());

                 // Several players: prefer the one that's playing.
                 struct State {
                     QStringList players;
                     QString playing;
                     int pending;
                 };
                 auto state = std::make_shared<State>(State{players, {}, int(players.size())});
                 for (const QString &p : std::as_const(players)) {
                     auto msg = QDBusMessage::createMethodCall(p, QStringLiteral("/org/mpris/MediaPlayer2"), QStringLiteral("org.freedesktop.DBus.Properties"),
                                                               QStringLiteral("Get"));
                     msg.setArguments({QStringLiteral("org.mpris.MediaPlayer2.Player"), QStringLiteral("PlaybackStatus")});
                     auto *w = new QDBusPendingCallWatcher(bus.asyncCall(msg, 2000), c.run.data());
                     QObject::connect(w, &QDBusPendingCallWatcher::finished, w, [state, p, send](QDBusPendingCallWatcher *w) {
                         w->deleteLater();
                         if (plain(w->reply().arguments().value(0)).toString() == QLatin1String("Playing") && state->playing.isEmpty())
                             state->playing = p;
                         if (--state->pending == 0)
                             send(state->playing.isEmpty() ? state->players.first() : state->playing);
                     });
                 }
             });
}

// --- Catalogue --------------------------------------------------------------------

QList<NodeType> build()
{
    const QString phone = QObject::tr("Phone"), desktop = QObject::tr("Desktop"), logic = QObject::tr("Logic"), data = QObject::tr("Data");
    QList<NodeType> t;

    t << NodeType{TriggerType,
                  QObject::tr("Phone command"),
                  QObject::tr("Trigger"),
                  QStringLiteral("bolt"),
                  QObject::tr("Runs when you tap this action in KDE Connect's Run command list on the phone, or press Run here."),
                  {},
                  {flowOut(), text(QStringLiteral("device"), QObject::tr("Phone name")), text(QStringLiteral("deviceId"), QObject::tr("Phone id"))},
                  {},
                  {},
                  220,
                  true};

    // --- Phone ---
    t << NodeType{QStringLiteral("phone.ring"), QObject::tr("Ring phone"), phone, QStringLiteral("ring_volume"),
                  QObject::tr("Rings the phone loudly, even when it's on silent."), {flowIn()}, {flowOut()},
                  [](const Ctx &c) { phoneCall(c, QStringLiteral("/findmyphone"), QStringLiteral("org.kde.kdeconnect.device.findmyphone"), QStringLiteral("ring")); },
                  {}, 200, false, QStringLiteral("kdeconnect_findmyphone")};

    t << NodeType{QStringLiteral("phone.ping"), QObject::tr("Ping phone"), phone, QStringLiteral("wifi_tethering"),
                  QObject::tr("Shows a notification on the phone."),
                  {flowIn(), text(QStringLiteral("message"), QObject::tr("Message"), {}, QObject::tr("Ping!"))}, {flowOut()},
                  [](const Ctx &c) {
                      const QString msg = c.str("message");
                      phoneCall(c, QStringLiteral("/ping"), QStringLiteral("org.kde.kdeconnect.device.ping"), QStringLiteral("sendPing"),
                                msg.isEmpty() ? QVariantList{} : QVariantList{msg});
                  },
                  {}, 240, false, QStringLiteral("kdeconnect_ping")};

    t << NodeType{QStringLiteral("phone.sms"), QObject::tr("Send SMS"), phone, QStringLiteral("sms"),
                  QObject::tr("Sends a text message from the phone. Separate several numbers with commas."),
                  {flowIn(), text(QStringLiteral("to"), QObject::tr("To"), {}, QStringLiteral("+1 555 0100")),
                   text(QStringLiteral("message"), QObject::tr("Message"))},
                  {flowOut()},
                  [](const Ctx &c) {
                      QVariantList addrs;
                      for (const QString &a : c.str("to").split(QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts))
                          if (!a.trimmed().isEmpty())
                              addrs << QVariant::fromValue(ConversationAddress{a.trimmed()});
                      if (addrs.isEmpty())
                          return c.fail(QObject::tr("No recipient"));
                      if (c.str("message").trimmed().isEmpty())
                          return c.fail(QObject::tr("Message is empty"));
                      phoneCall(c, {}, QStringLiteral("org.kde.kdeconnect.device.conversations"), QStringLiteral("sendWithoutConversation"),
                                {addrs, c.str("message"), QVariantList{}});
                  },
                  {}, 260, false, QStringLiteral("kdeconnect_sms")};

    t << NodeType{QStringLiteral("phone.share"), QObject::tr("Send to phone"), phone, QStringLiteral("send_to_mobile"),
                  QObject::tr("Sends a file, opens a link, or copies text on the phone. \"auto\" picks based on what the content looks like."),
                  {flowIn(), text(QStringLiteral("content"), QObject::tr("Content"), {}, QObject::tr("text, link or ~/file")),
                   choice(QStringLiteral("as"), QObject::tr("As"), {QStringLiteral("auto"), QStringLiteral("text"), QStringLiteral("link"), QStringLiteral("file")})},
                  {flowOut()},
                  [](const Ctx &c) {
                      const QString content = c.str("content");
                      QString as = c.str("as");
                      const QString path = expandHome(content);
                      if (as == QLatin1String("auto")) {
                          const QUrl url(content.trimmed(), QUrl::StrictMode);
                          if (!path.isEmpty() && QFileInfo(path).isFile())
                              as = QStringLiteral("file");
                          else if (url.isValid() && !url.scheme().isEmpty() && !content.trimmed().contains(QLatin1Char(' ')))
                              as = QStringLiteral("link");
                          else
                              as = QStringLiteral("text");
                      }
                      const QString iface = QStringLiteral("org.kde.kdeconnect.device.share");
                      if (as == QLatin1String("file")) {
                          if (!QFileInfo(path).isFile())
                              return c.fail(QObject::tr("No such file: %1").arg(path));
                          return phoneCall(c, QStringLiteral("/share"), iface, QStringLiteral("shareUrl"),
                                           {QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString()});
                      }
                      if (content.trimmed().isEmpty())
                          return c.fail(QObject::tr("Nothing to send"));
                      if (as == QLatin1String("link"))
                          return phoneCall(c, QStringLiteral("/share"), iface, QStringLiteral("shareUrl"), {content.trimmed()});
                      phoneCall(c, QStringLiteral("/share"), iface, QStringLiteral("shareText"), {content});
                  },
                  {}, 270, false, QStringLiteral("kdeconnect_share")};

    t << NodeType{QStringLiteral("phone.clipboard"), QObject::tr("Send clipboard"), phone, QStringLiteral("content_paste"),
                  QObject::tr("Pushes this computer's clipboard to the phone."), {flowIn()}, {flowOut()},
                  [](const Ctx &c) { phoneCall(c, QStringLiteral("/clipboard"), QStringLiteral("org.kde.kdeconnect.device.clipboard"), QStringLiteral("sendClipboard")); },
                  {}, 200, false, QStringLiteral("kdeconnect_clipboard")};

    t << NodeType{QStringLiteral("phone.media"), QObject::tr("Phone media"), phone, QStringLiteral("play_circle"),
                  QObject::tr("Controls what's playing on the phone."),
                  {flowIn(), choice(QStringLiteral("action"), QObject::tr("Action"),
                                    {QStringLiteral("Play/Pause"), QStringLiteral("Play"), QStringLiteral("Pause"), QStringLiteral("Next"), QStringLiteral("Previous"),
                                     QStringLiteral("Stop")})},
                  {flowOut()},
                  [](const Ctx &c) {
                      phoneCall(c, QStringLiteral("/mprisremote"), QStringLiteral("org.kde.kdeconnect.device.mprisremote"), QStringLiteral("sendAction"),
                                {methodName(c.str("action"))});
                  },
                  {}, 230, false, QStringLiteral("kdeconnect_mprisremote")};

    t << NodeType{QStringLiteral("phone.battery"), QObject::tr("Phone battery"), phone, QStringLiteral("battery_full"),
                  QObject::tr("Reads the phone's battery level."), {flowIn()},
                  {flowOut(), number(QStringLiteral("charge"), QObject::tr("Charge %")), {QStringLiteral("charging"), QObject::tr("Charging"), PinKind::Bool}},
                  [](const Ctx &c) {
                      phoneCall(c, QStringLiteral("/battery"), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"),
                                {QStringLiteral("org.kde.kdeconnect.device.battery")}, [c](const QDBusMessage &reply) {
                                    const QVariantMap p = plain(reply.arguments().value(0)).toMap();
                                    c.next({{QStringLiteral("charge"), plain(p.value(QStringLiteral("charge"))).toInt()},
                                            {QStringLiteral("charging"), plain(p.value(QStringLiteral("isCharging"))).toBool()}});
                                });
                  },
                  {}, 220, false, QStringLiteral("kdeconnect_battery")};

    // --- Desktop ---
    t << NodeType{QStringLiteral("desktop.shell"), QObject::tr("Run shell command"), desktop, QStringLiteral("terminal"),
                  QObject::tr("Runs a command with /bin/sh. Continues on \"failed\" when it exits non-zero or times out."),
                  {flowIn(), text(QStringLiteral("command"), QObject::tr("Command"), {}, QStringLiteral("notify-send hi")),
                   number(QStringLiteral("timeout"), QObject::tr("Timeout s"), 30)},
                  {flowOut(), flowOut(QStringLiteral("failed"), QObject::tr("failed")), text(QStringLiteral("stdout"), QObject::tr("Output")),
                   number(QStringLiteral("exitCode"), QObject::tr("Exit code"))},
                  [](const Ctx &c) {
                      const QString command = c.str("command");
                      if (command.trimmed().isEmpty())
                          return c.fail(QObject::tr("No command"));
                      auto *p = new QProcess(c.run.data());
                      auto *timer = new QTimer(p);
                      timer->setSingleShot(true);
                      const double timeout = c.num("timeout");
                      QObject::connect(timer, &QTimer::timeout, p, [c, p, timeout] {
                          c.log(QObject::tr("Timed out after %1 s").arg(timeout));
                          p->disconnect();
                          p->kill();
                          p->deleteLater();
                          c.branch(QStringLiteral("failed"), {{QStringLiteral("exitCode"), -1}});
                      });
                      QObject::connect(p, &QProcess::errorOccurred, p, [c, p](QProcess::ProcessError e) {
                          if (e != QProcess::FailedToStart)
                              return;
                          p->disconnect();
                          p->deleteLater();
                          c.fail(p->errorString());
                      });
                      QObject::connect(p, &QProcess::finished, p, [c, p](int code, QProcess::ExitStatus status) {
                          p->disconnect();
                          p->deleteLater();
                          const QString out = QString::fromLocal8Bit(p->readAllStandardOutput()).trimmed();
                          const QString err = QString::fromLocal8Bit(p->readAllStandardError()).trimmed();
                          if (!err.isEmpty())
                              c.log(err.left(500));
                          const bool ok = status == QProcess::NormalExit && code == 0;
                          c.branch(ok ? QStringLiteral("out") : QStringLiteral("failed"),
                                   {{QStringLiteral("stdout"), out}, {QStringLiteral("exitCode"), status == QProcess::NormalExit ? code : -1}});
                      });
                      p->setProcessChannelMode(QProcess::SeparateChannels);
                      p->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
                      if (timeout > 0)
                          timer->start(int(std::min(timeout, 86400.0) * 1000));
                  },
                  {}, 290};

    t << NodeType{QStringLiteral("desktop.notify"), QObject::tr("Show notification"), desktop, QStringLiteral("notifications"),
                  QObject::tr("Pops up a desktop notification."),
                  {flowIn(), text(QStringLiteral("title"), QObject::tr("Title"), QObject::tr("Slonnect")), text(QStringLiteral("body"), QObject::tr("Body"))},
                  {flowOut()},
                  [](const Ctx &c) {
                      dbusCall(c, QDBusConnection::sessionBus(), QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                               QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"),
                               {QStringLiteral("Slonnect"), 0u, QStringLiteral("kdeconnect"), c.str("title"), c.str("body"), QStringList{},
                                QVariantMap{{QStringLiteral("desktop-entry"), QVariant::fromValue(QDBusVariant(QStringLiteral("dev.slonnect.Slonnect")))}}, -1},
                               [c](const QDBusMessage &) { c.next(); });
                  },
                  {}, 260};

    t << NodeType{QStringLiteral("desktop.open"), QObject::tr("Open URL or file"), desktop, QStringLiteral("open_in_new"),
                  QObject::tr("Opens a link, file or folder in its default app."),
                  {flowIn(), text(QStringLiteral("target"), QObject::tr("URL / path"), {}, QStringLiteral("https://…"))}, {flowOut()},
                  [](const Ctx &c) {
                      const QString target = expandHome(c.str("target"));
                      if (target.isEmpty())
                          return c.fail(QObject::tr("Nothing to open"));
                      const QUrl url = QUrl::fromUserInput(target, QDir::homePath(), QUrl::AssumeLocalFile);
                      if (!QDesktopServices::openUrl(url))
                          return c.fail(QObject::tr("Couldn't open %1").arg(target));
                      c.next();
                  },
                  {}, 260};

    t << NodeType{QStringLiteral("desktop.lock"), QObject::tr("Lock screen"), desktop, QStringLiteral("lock"), QObject::tr("Locks this computer."),
                  {flowIn()}, {flowOut()},
                  [](const Ctx &c) {
                      auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"),
                                                                QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("Lock"));
                      auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), c.run.data());
                      QObject::connect(w, &QDBusPendingCallWatcher::finished, w, [c](QDBusPendingCallWatcher *w) {
                          w->deleteLater();
                          // Not every desktop implements the ScreenSaver interface; logind does.
                          if (w->isError() && !QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("lock-session")}))
                              return c.fail(w->error().message());
                          c.next();
                      });
                  },
                  {}, 200};

    t << NodeType{QStringLiteral("desktop.media"), QObject::tr("Desktop media"), desktop, QStringLiteral("music_note"),
                  QObject::tr("Controls a media player on this computer (MPRIS). Leave Player empty to use whichever is playing."),
                  {flowIn(),
                   choice(QStringLiteral("action"), QObject::tr("Action"),
                          {QStringLiteral("Play/Pause"), QStringLiteral("Play"), QStringLiteral("Pause"), QStringLiteral("Next"), QStringLiteral("Previous"), QStringLiteral("Stop")}),
                   text(QStringLiteral("player"), QObject::tr("Player"), {}, QObject::tr("any"))},
                  {flowOut(), text(QStringLiteral("player"), QObject::tr("Player"))}, desktopMedia, {}, 250};

    t << NodeType{QStringLiteral("desktop.dbus"), QObject::tr("D-Bus call"), desktop, QStringLiteral("code"),
                  QObject::tr("Calls any D-Bus method. Arguments are a JSON array, e.g. [\"text\", 3, true]."),
                  {flowIn(), choice(QStringLiteral("bus"), QObject::tr("Bus"), {QStringLiteral("session"), QStringLiteral("system")}),
                   text(QStringLiteral("service"), QObject::tr("Service"), {}, QStringLiteral("org.kde.KWin")),
                   text(QStringLiteral("path"), QObject::tr("Path"), QStringLiteral("/")), text(QStringLiteral("interface"), QObject::tr("Interface")),
                   text(QStringLiteral("method"), QObject::tr("Method")), text(QStringLiteral("args"), QObject::tr("Args"), {}, QStringLiteral("[]"))},
                  {flowOut(), text(QStringLiteral("result"), QObject::tr("Result"))},
                  [](const Ctx &c) {
                      QVariantList args;
                      const QString json = c.str("args").trimmed();
                      if (!json.isEmpty()) {
                          QJsonParseError err;
                          const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
                          if (!doc.isArray())
                              return c.fail(QObject::tr("Args must be a JSON array: %1").arg(err.errorString()));
                          for (const auto &v : doc.array())
                              args << fromJson(v);
                      }
                      if (c.str("service").trimmed().isEmpty() || c.str("method").trimmed().isEmpty())
                          return c.fail(QObject::tr("Service and method are required"));
                      const auto bus = c.str("bus") == QLatin1String("system") ? QDBusConnection::systemBus() : QDBusConnection::sessionBus();
                      dbusCall(c, bus, c.str("service").trimmed(), c.str("path").trimmed(), c.str("interface").trimmed(), c.str("method").trimmed(), args,
                               [c](const QDBusMessage &reply) {
                                   QStringList parts;
                                   for (const QVariant &v : reply.arguments())
                                       parts << toText(v);
                                   c.next({{QStringLiteral("result"), parts.join(QLatin1Char('\n'))}});
                               });
                  },
                  {}, 300};

    // --- Logic ---
    t << NodeType{QStringLiteral("logic.if"), QObject::tr("If"), logic, QStringLiteral("call_split"),
                  QObject::tr("Takes one of two paths. Compares as numbers when both sides are numbers."),
                  {flowIn(), text(QStringLiteral("a"), QStringLiteral("A")),
                   choice(QStringLiteral("op"), QObject::tr("Is"),
                          {QStringLiteral("="), QStringLiteral("≠"), QStringLiteral("contains"), QStringLiteral(">"), QStringLiteral("<"), QStringLiteral("is empty"),
                           QStringLiteral("is not empty")}),
                   text(QStringLiteral("b"), QStringLiteral("B"))},
                  {flowOut(QStringLiteral("true"), QObject::tr("true")), flowOut(QStringLiteral("false"), QObject::tr("false"))},
                  [](const Ctx &c) {
                      const bool r = compare(c.in.value(QStringLiteral("a")), c.str("op"), c.in.value(QStringLiteral("b")));
                      c.branch(r ? QStringLiteral("true") : QStringLiteral("false"));
                  },
                  {}, 240};

    t << NodeType{QStringLiteral("logic.wait"), QObject::tr("Wait"), logic, QStringLiteral("timer"), QObject::tr("Pauses before continuing."),
                  {flowIn(), number(QStringLiteral("seconds"), QObject::tr("Seconds"), 1)}, {flowOut()},
                  [](const Ctx &c) {
                      const int ms = int(std::clamp(c.num("seconds"), 0.0, 3600.0) * 1000);
                      QTimer::singleShot(ms, c.run.data(), [c] { c.next(); });
                  },
                  {}, 200};

    t << NodeType{QStringLiteral("logic.sequence"), QObject::tr("Sequence"), logic, QStringLiteral("format_list_numbered"),
                  QObject::tr("Runs each branch to the end, one after another."), {flowIn()},
                  {flowOut(QStringLiteral("then1"), QObject::tr("then 1")), flowOut(QStringLiteral("then2"), QObject::tr("then 2")),
                   flowOut(QStringLiteral("then3"), QObject::tr("then 3"))},
                  [](const Ctx &c) { c.done({QStringLiteral("then1"), QStringLiteral("then2"), QStringLiteral("then3")}, {}); }, {}, 180};

    t << NodeType{QStringLiteral("logic.log"), QObject::tr("Log"), logic, QStringLiteral("subject"), QObject::tr("Writes a line to the run log."),
                  {flowIn(), text(QStringLiteral("message"), QObject::tr("Message"))}, {flowOut()},
                  [](const Ctx &c) {
                      c.log(toText(c.in.value(QStringLiteral("message"))));
                      c.next();
                  },
                  {}, 240};

    // --- Data (pure) ---
    t << NodeType{QStringLiteral("data.text"), QObject::tr("Text"), data, QStringLiteral("text_fields"), QObject::tr("A piece of text to reuse."),
                  {text(QStringLiteral("value"), QString())}, {text(QStringLiteral("out"), QObject::tr("Text"))}, {},
                  [](const QVariantMap &in) { return QVariantMap{{QStringLiteral("out"), in.value(QStringLiteral("value"))}}; }, 220};

    t << NodeType{QStringLiteral("data.format"), QObject::tr("Format text"), data, QStringLiteral("data_object"),
                  QObject::tr("Fills {a}, {b} and {c} in the template."),
                  {text(QStringLiteral("template"), QObject::tr("Template"), QStringLiteral("{a}")), text(QStringLiteral("a"), QStringLiteral("a")),
                   text(QStringLiteral("b"), QStringLiteral("b")), text(QStringLiteral("c"), QStringLiteral("c"))},
                  {text(QStringLiteral("out"), QObject::tr("Text"))}, {},
                  [](const QVariantMap &in) {
                      QString s = in.value(QStringLiteral("template")).toString();
                      for (const char *k : {"a", "b", "c"})
                          s.replace(QLatin1Char('{') + QLatin1String(k) + QLatin1Char('}'), toText(in.value(QLatin1String(k))));
                      return QVariantMap{{QStringLiteral("out"), s}};
                  },
                  260};

    t << NodeType{QStringLiteral("data.datetime"), QObject::tr("Date & time"), data, QStringLiteral("schedule"),
                  QObject::tr("The current time. Format uses Qt patterns: yyyy MM dd HH mm ss ddd."),
                  {text(QStringLiteral("format"), QObject::tr("Format"), QStringLiteral("yyyy-MM-dd HH:mm"))},
                  {text(QStringLiteral("out"), QObject::tr("Text")), number(QStringLiteral("unix"), QObject::tr("Unix time"))}, {},
                  [](const QVariantMap &in) {
                      const QDateTime now = QDateTime::currentDateTime();
                      return QVariantMap{{QStringLiteral("out"), now.toString(in.value(QStringLiteral("format")).toString())},
                                         {QStringLiteral("unix"), now.toSecsSinceEpoch()}};
                  },
                  240};

    t << NodeType{QStringLiteral("data.math"), QObject::tr("Math"), data, QStringLiteral("calculate"), QObject::tr("Arithmetic on two numbers."),
                  {number(QStringLiteral("a"), QStringLiteral("A")),
                   choice(QStringLiteral("op"), QObject::tr("Op"),
                          {QStringLiteral("+"), QStringLiteral("−"), QStringLiteral("×"), QStringLiteral("÷"), QStringLiteral("min"), QStringLiteral("max")}),
                   number(QStringLiteral("b"), QStringLiteral("B"))},
                  {number(QStringLiteral("out"), QObject::tr("Result"))}, {},
                  [](const QVariantMap &in) {
                      const double a = in.value(QStringLiteral("a")).toDouble(), b = in.value(QStringLiteral("b")).toDouble();
                      const QString op = in.value(QStringLiteral("op")).toString();
                      double r = 0;
                      if (op == QLatin1String("+"))
                          r = a + b;
                      else if (op == QStringLiteral("−"))
                          r = a - b;
                      else if (op == QStringLiteral("×"))
                          r = a * b;
                      else if (op == QStringLiteral("÷"))
                          r = b == 0 ? 0 : a / b;
                      else if (op == QLatin1String("min"))
                          r = std::min(a, b);
                      else if (op == QLatin1String("max"))
                          r = std::max(a, b);
                      return QVariantMap{{QStringLiteral("out"), r}};
                  },
                  210};

    return t;
}

} // namespace

QVariantList describePins(const QList<PinDef> &pins)
{
    QVariantList list;
    for (const auto &p : pins)
        list << QVariantMap{{QStringLiteral("id"), p.id},
                            {QStringLiteral("label"), p.label},
                            {QStringLiteral("kind"), kindName(p.kind)},
                            {QStringLiteral("defaultValue"), p.defaultValue},
                            {QStringLiteral("options"), p.options},
                            {QStringLiteral("placeholder"), p.placeholder}};
    return list;
}

const QList<NodeType> &all()
{
    static const QList<NodeType> types = build();
    return types;
}

const NodeType *find(const QString &id)
{
    for (const auto &t : all())
        if (t.id == id)
            return &t;
    return nullptr;
}

QVariantMap describe(const NodeType &t)
{
    return {{QStringLiteral("id"), t.id},
            {QStringLiteral("title"), t.title},
            {QStringLiteral("category"), t.category},
            {QStringLiteral("icon"), t.icon},
            {QStringLiteral("description"), t.description},
            {QStringLiteral("inputs"), describePins(t.inputs)},
            {QStringLiteral("outputs"), describePins(t.outputs)},
            {QStringLiteral("width"), t.width},
            {QStringLiteral("trigger"), t.trigger},
            {QStringLiteral("pure"), t.isPure()},
            {QStringLiteral("plugin"), t.plugin}};
}

QVariantList catalog()
{
    QVariantList list;
    for (const auto &t : all())
        list << describe(t);
    return list;
}

} // namespace nodes
