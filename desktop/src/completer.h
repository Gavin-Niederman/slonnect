#pragma once

#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Suggestions for node inputs, looked up live: D-Bus services/paths/interfaces/
// methods, paired Bluetooth devices, MPRIS players. Asynchronous: complete() returns a
// request id and completed() delivers [{ value, detail, fill? }] for it. `fill` holds
// other inputs to prefill (e.g. an args skeleton for a chosen method).
class Completer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.completer")

public:
    explicit Completer(QObject *parent = nullptr);

    // kind: "dbus.service" | "dbus.path" | "dbus.interface" | "dbus.method" |
    //       "bluetooth.device" | "mpris.player". context: the node's current values.
    Q_INVOKABLE int complete(const QString &kind, const QVariantMap &context);

Q_SIGNALS:
    void completed(int requestId, const QVariantList &items);

private:
    void finish(int id, const QVariantList &items, const QString &cacheKey = {});
    void services(int id, bool system, const QString &cacheKey);
    void paths(int id, bool system, const QString &service, const QString &cacheKey);

    struct Cached {
        qint64 at = 0;
        QVariantList items;
    };
    QHash<QString, Cached> m_cache;
    int m_nextId = 1;
};
