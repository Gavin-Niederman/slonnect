// Helpers shared by the node implementations (nodetypes.cpp, systemnodes.cpp).
#pragma once

#include "nodetypes.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonValue>
#include <QMap>

namespace nodes::detail {

using Reply = std::function<void(const QDBusMessage &)>;

// D-Bus value -> plain QVariant (lists, maps, strings, numbers; object paths as strings).
QVariant plain(const QVariant &v);
// Plain text for logs and text pins; lists and maps as compact JSON.
QString toText(const QVariant &v);
// JSON -> D-Bus-friendly QVariant when the method's signature is unknown.
QVariant fromJson(const QJsonValue &v);
QString expandHome(QString s);

// Async method call; failures end the node with the D-Bus error.
void dbusCall(const Ctx &c, const QDBusConnection &bus, const QString &service, const QString &path, const QString &iface, const QString &method,
              const QVariantList &args, Reply onReply, int timeoutMs = -1);

// Run a program (no shell) and hand over its stdout; non-zero exit fails the node.
void runTool(const Ctx &c, const QString &program, const QStringList &args, std::function<void(const QString &out)> onDone, int timeoutMs = 15000);

PinDef flowIn();
PinDef flowOut(const QString &id = QStringLiteral("out"), const QString &label = {});
PinDef text(const QString &id, const QString &label, const QString &def = {}, const QString &placeholder = {});
PinDef number(const QString &id, const QString &label, double def = 0);
PinDef boolean(const QString &id, const QString &label);
PinDef choice(const QString &id, const QString &label, const QStringList &options);
// Same pin, with editor suggestions of `kind` (see Completer).
PinDef completing(PinDef pin, const QString &kind);

// BlueZ objects from org.freedesktop.DBus.ObjectManager.GetManagedObjects on org.bluez.
struct BluezDevice {
    QString path;
    QString name;
    QString address;
    QString adapter;
    bool connected = false;
    bool paired = false;
};
struct BluezState {
    QMap<QString, bool> adapters; // path -> powered
    QList<BluezDevice> devices;
};
BluezState parseBluez(const QVariant &managedObjects);

// Audio, brightness and Bluetooth nodes (systemnodes.cpp).
void addSystemNodes(QList<NodeType> &types);

} // namespace nodes::detail
