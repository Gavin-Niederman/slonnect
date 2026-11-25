// Thin async helpers around the kdeconnectd session-bus API.
#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QString>
#include <QVariant>
#include <functional>

namespace kdc {

inline const QString Service = QStringLiteral("org.kde.kdeconnect");
inline const QString DaemonPath = QStringLiteral("/modules/kdeconnect");
inline const QString DaemonIface = QStringLiteral("org.kde.kdeconnect.daemon");
inline const QString DeviceIface = QStringLiteral("org.kde.kdeconnect.device");

QDBusConnection bus();
QString devicePath(const QString &deviceId);

using ReplyFn = std::function<void(const QDBusMessage &reply)>;

// Fire an async method call. `onReply` runs on success only, and only while
// `context` is alive; errors are logged.
void call(const QString &path, const QString &iface, const QString &method, const QVariantList &args = {}, QObject *context = nullptr,
          ReplyFn onReply = {});

void getAll(const QString &path, const QString &iface, QObject *context, std::function<void(const QVariantMap &)> onReply);
void setProperty(const QString &path, const QString &iface, const QString &name, const QVariant &value);

// Unwrap QDBusVariant / nested variants down to a plain QVariant.
QVariant unwrap(const QVariant &v);

} // namespace kdc
