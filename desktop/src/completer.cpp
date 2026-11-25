#include "completer.h"
#include "dbusintrospect.h"
#include "nodes_p.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDateTime>
#include <QSet>
#include <memory>

namespace {

constexpr qint64 CacheMs = 15000;
// Bound on objects visited when listing a service's paths (some export thousands).
constexpr int MaxPathNodes = 400;

const QSet<QString> &standardInterfaces()
{
    static const QSet<QString> s{QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("org.freedesktop.DBus.Peer"),
                                 QStringLiteral("org.freedesktop.DBus.Properties")};
    return s;
}

QVariantMap item(const QString &value, const QString &detail = {}, const QVariantMap &fill = {})
{
    QVariantMap m{{QStringLiteral("value"), value}, {QStringLiteral("detail"), detail}};
    if (!fill.isEmpty())
        m.insert(QStringLiteral("fill"), fill);
    return m;
}

QDBusConnection busFor(bool system)
{
    return system ? QDBusConnection::systemBus() : QDBusConnection::sessionBus();
}

// Async call whose reply (or empty message on error) goes to `done`.
void call(QObject *context, const QDBusConnection &bus, const QString &service, const QString &path, const QString &iface, const QString &method,
          std::function<void(const QDBusMessage &)> done)
{
    auto msg = QDBusMessage::createMethodCall(service, path, iface, method);
    auto *w = new QDBusPendingCallWatcher(bus.asyncCall(msg, 5000), context);
    QObject::connect(w, &QDBusPendingCallWatcher::finished, w, [done](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        done(w->reply().type() == QDBusMessage::ReplyMessage ? w->reply() : QDBusMessage());
    });
}

} // namespace

Completer::Completer(QObject *parent)
    : QObject(parent)
{
}

int Completer::complete(const QString &kind, const QVariantMap &context)
{
    const int id = m_nextId++;
    const bool system = context.value(QStringLiteral("bus")).toString() == QLatin1String("system");
    const QString service = context.value(QStringLiteral("service")).toString().trimmed();
    const QString path = context.value(QStringLiteral("path")).toString().trimmed();
    const QString iface = context.value(QStringLiteral("interface")).toString().trimmed();

    const QString key = kind + QLatin1Char('|') + QString::number(system) + QLatin1Char('|') + service + QLatin1Char('|') + path + QLatin1Char('|') + iface;
    if (const auto it = m_cache.constFind(key); it != m_cache.cend() && QDateTime::currentMSecsSinceEpoch() - it->at < CacheMs) {
        finish(id, it->items);
        return id;
    }

    if (kind == QLatin1String("dbus.service")) {
        services(id, system, key);
    } else if (kind == QLatin1String("dbus.path")) {
        paths(id, system, service, key);
    } else if (kind == QLatin1String("dbus.interface") || kind == QLatin1String("dbus.method")) {
        const bool methods = kind == QLatin1String("dbus.method");
        introspect::fetch(busFor(system), service, path.isEmpty() ? QStringLiteral("/") : path, this,
                          [this, id, key, methods, iface](bool ok, const introspect::Node &node) {
                              QVariantList items;
                              for (const auto &i : node.interfaces) {
                                  if (!methods) {
                                      if (i.name != QLatin1String("org.freedesktop.DBus.Introspectable") && i.name != QLatin1String("org.freedesktop.DBus.Peer"))
                                          items << item(i.name, (i.methods.size() == 1 ? tr("1 method") : tr("%1 methods").arg(i.methods.size()))
                                                                    + (i.properties.isEmpty() ? QString() : i.properties.size() == 1 ? tr(", 1 property") : tr(", %1 properties").arg(i.properties.size())));
                                      continue;
                                  }
                                  // Methods of the chosen interface, or of all of them when none is chosen yet.
                                  if (!iface.isEmpty() && i.name != iface)
                                      continue;
                                  for (const auto &m : i.methods) {
                                      QStringList params;
                                      const QStringList types = introspect::splitSignature(m.in);
                                      for (int k = 0; k < types.size(); ++k)
                                          params << (m.inNames.value(k).isEmpty() ? types.at(k) : m.inNames.value(k) + QLatin1Char(' ') + types.at(k));
                                      QString detail = QLatin1Char('(') + params.join(QStringLiteral(", ")) + QLatin1Char(')');
                                      if (!m.out.isEmpty())
                                          detail += QStringLiteral(" → ") + m.out;
                                      if (iface.isEmpty())
                                          detail = i.name + QStringLiteral(" · ") + detail;
                                      QVariantMap fill{{QStringLiteral("args"), introspect::argsSkeleton(m.in)}};
                                      if (iface.isEmpty())
                                          fill.insert(QStringLiteral("interface"), i.name);
                                      items << item(m.name, detail, fill);
                                  }
                              }
                              finish(id, items, ok ? key : QString());
                          });
    } else if (kind == QLatin1String("bluetooth.device")) {
        call(this, QDBusConnection::systemBus(), QStringLiteral("org.bluez"), QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
             QStringLiteral("GetManagedObjects"), [this, id, key](const QDBusMessage &reply) {
                 QVariantList items;
                 const auto state = nodes::detail::parseBluez(reply.arguments().value(0));
                 for (const auto &d : state.devices) {
                     QString detail = d.address;
                     if (d.connected)
                         detail += tr(" · connected");
                     else if (!d.paired)
                         detail += tr(" · not paired");
                     items << item(d.name.isEmpty() ? d.address : d.name, detail);
                 }
                 finish(id, items, reply.type() == QDBusMessage::ReplyMessage ? key : QString());
             });
    } else if (kind == QLatin1String("mpris.player")) {
        call(this, QDBusConnection::sessionBus(), QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
             QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ListNames"), [this, id](const QDBusMessage &reply) {
                 QVariantList items;
                 const QString prefix = QStringLiteral("org.mpris.MediaPlayer2.");
                 for (const QString &n : reply.arguments().value(0).toStringList())
                     if (n.startsWith(prefix))
                         items << item(n.mid(prefix.size()), n);
                 finish(id, items); // players come and go; no cache
             });
    } else {
        finish(id, {});
    }
    return id;
}

void Completer::finish(int id, const QVariantList &items, const QString &cacheKey)
{
    if (!cacheKey.isEmpty())
        m_cache.insert(cacheKey, Cached{QDateTime::currentMSecsSinceEpoch(), items});
    // Always async, so the caller has stored the id before results arrive.
    QMetaObject::invokeMethod(this, [this, id, items] { Q_EMIT completed(id, items); }, Qt::QueuedConnection);
}

void Completer::services(int id, bool system, const QString &cacheKey)
{
    struct State {
        QStringList running, activatable;
        int pending = 2;
    };
    auto state = std::make_shared<State>();
    auto done = [this, id, cacheKey, state] {
        if (--state->pending > 0)
            return;
        QStringList names = state->running;
        for (const QString &n : std::as_const(state->activatable))
            if (!names.contains(n))
                names << n;
        names.removeIf([](const QString &n) { return n.startsWith(QLatin1Char(':')); });
        names.sort(Qt::CaseInsensitive);
        QVariantList items;
        for (const QString &n : std::as_const(names))
            items << item(n, state->running.contains(n) ? QString() : tr("starts on demand"));
        finish(id, items, cacheKey);
    };
    const auto bus = busFor(system);
    for (const char *method : {"ListNames", "ListActivatableNames"}) {
        const bool running = qstrcmp(method, "ListNames") == 0;
        call(this, bus, QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
             QLatin1String(method), [state, running, done](const QDBusMessage &reply) {
                 (running ? state->running : state->activatable) = reply.arguments().value(0).toStringList();
                 done();
             });
    }
}

void Completer::paths(int id, bool system, const QString &service, const QString &cacheKey)
{
    if (service.isEmpty())
        return finish(id, {});
    // Breadth-first walk of the object tree via Introspect, bounded by MaxPathNodes.
    struct State {
        QVariantList items;
        int pending = 0;
        int visited = 0;
    };
    auto state = std::make_shared<State>();
    const auto bus = busFor(system);
    auto visit = std::make_shared<std::function<void(const QString &)>>();
    *visit = [this, id, cacheKey, state, bus, service, weak = std::weak_ptr(visit)](const QString &path) {
        ++state->pending;
        ++state->visited;
        // Each pending callback keeps the walker alive; it goes away with the last one.
        introspect::fetch(bus, service, path, this, [this, id, cacheKey, state, path, self = weak.lock()](bool ok, const introspect::Node &node) {
            if (ok) {
                QStringList own;
                for (const auto &i : node.interfaces)
                    if (!standardInterfaces().contains(i.name))
                        own << i.name;
                own.removeDuplicates();
                if (!own.isEmpty())
                    state->items << item(path, own.join(QStringLiteral(", ")));
                for (const QString &child : node.children) {
                    if (state->visited >= MaxPathNodes)
                        break;
                    (*self)(path == QLatin1String("/") ? QLatin1Char('/') + child : path + QLatin1Char('/') + child);
                }
            }
            if (--state->pending == 0) {
                std::sort(state->items.begin(), state->items.end(), [](const QVariant &a, const QVariant &b) {
                    return a.toMap().value(QStringLiteral("value")).toString() < b.toMap().value(QStringLiteral("value")).toString();
                });
                finish(id, state->items, cacheKey);
            }
        });
    };
    (*visit)(QStringLiteral("/"));
}
