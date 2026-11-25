// D-Bus introspection: what's at a path, and turning loosely typed values (JSON from
// the D-Bus call node) into the exact types a method's signature asks for.
#pragma once

#include <QDBusConnection>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <functional>

namespace introspect {

struct Method {
    QString name;
    QString in;  // input signature, e.g. "susssasa{sv}i"
    QString out; // output signature
    QStringList inNames;
};

struct Interface {
    QString name;
    QList<Method> methods;
    QStringList properties;
};

struct Node {
    QStringList children; // relative names
    QList<Interface> interfaces;

    const Interface *interface(const QString &name) const;
};

Node parse(const QString &xml);

// Async Introspect(); `done` gets ok=false on errors. Only while `context` lives.
void fetch(const QDBusConnection &bus, const QString &service, const QString &path, QObject *context, std::function<void(bool ok, const Node &node)> done);

// "sa{sv}i" -> {"s", "a{sv}", "i"}.
QStringList splitSignature(const QString &signature);

// Convert `value` (QString, double, bool, QVariantList, QVariantMap from JSON) to the
// complete D-Bus type `type`. Sets *ok=false when it can't.
QVariant coerce(const QVariant &value, const QString &type, bool *ok);

// A JSON array literal with a placeholder per argument, e.g. "[\"\", 0, {}]".
QString argsSkeleton(const QString &signature);

} // namespace introspect
