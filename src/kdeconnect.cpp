#include "kdeconnect.h"

#include <QDBusArgument>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QLoggingCategory>
#include <QPointer>

Q_LOGGING_CATEGORY(lcDbus, "slonnect.dbus")

namespace kdc {

QDBusConnection bus()
{
    return QDBusConnection::sessionBus();
}

QString devicePath(const QString &deviceId)
{
    return DaemonPath + QStringLiteral("/devices/") + deviceId;
}

void call(const QString &path, const QString &iface, const QString &method, const QVariantList &args, QObject *context, ReplyFn onReply)
{
    auto msg = QDBusMessage::createMethodCall(Service, path, iface, method);
    msg.setArguments(args);
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(msg), context);
    QPointer<QObject> guard(context);
    const bool hasContext = context != nullptr;
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [=](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qCWarning(lcDbus) << method << "on" << path << "failed:" << reply.errorMessage();
            return;
        }
        if (onReply && (!hasContext || guard))
            onReply(reply);
    });
}

void getAll(const QString &path, const QString &iface, QObject *context, std::function<void(const QVariantMap &)> onReply)
{
    call(path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"), {iface}, context, [onReply](const QDBusMessage &reply) {
        if (reply.arguments().isEmpty())
            return;
        const QVariant arg = reply.arguments().constFirst();
        QVariantMap map;
        if (arg.canConvert<QDBusArgument>())
            arg.value<QDBusArgument>() >> map;
        else
            map = arg.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            it.value() = unwrap(it.value());
        onReply(map);
    });
}

void setProperty(const QString &path, const QString &iface, const QString &name, const QVariant &value)
{
    call(path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"), {iface, name, QVariant::fromValue(QDBusVariant(value))});
}

QVariant unwrap(const QVariant &v)
{
    if (v.metaType() == QMetaType::fromType<QDBusVariant>())
        return unwrap(v.value<QDBusVariant>().variant());
    if (v.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const auto arg = v.value<QDBusArgument>();
        if (arg.currentType() == QDBusArgument::ArrayType && arg.currentSignature() == QLatin1String("as")) {
            QStringList list;
            arg >> list;
            return list;
        }
    }
    return v;
}

} // namespace kdc
