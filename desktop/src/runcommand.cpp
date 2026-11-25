#include "runcommand.h"
#include "kdeconnect.h"

#include <QCoreApplication>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSettings>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcRunCommand, "slonnect.runcommand")

namespace runcommand {

namespace {

const QString CommandsKey = QStringLiteral("commands");

QString shellQuote(const QString &s)
{
    QString q = s;
    q.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

// Path of the executable the phone should launch. Nix's wrapQtAppsHook runs the real
// binary as bin/.slonnect-wrapped; the wrapper next to it sets up Qt's plugin paths.
QString launcher()
{
    if (const QString env = qEnvironmentVariable("SLONNECT_LAUNCHER"); !env.isEmpty())
        return env;
    const QFileInfo self(QCoreApplication::applicationFilePath());
    QString exe = self.absoluteFilePath();
    const QString name = self.fileName();
    if (name.startsWith(QLatin1Char('.')) && name.endsWith(QLatin1String("-wrapped"))) {
        const QString wrapper = self.absolutePath() + QLatin1Char('/') + name.mid(1, name.size() - 1 - 8);
        if (QFileInfo(wrapper).isExecutable())
            exe = wrapper;
    }
    // Prefer the PATH entry (e.g. a Nix profile symlink) when it is this same binary:
    // it survives updates, where the /nix/store path changes with every build.
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("slonnect"));
    if (!onPath.isEmpty() && QFileInfo(onPath).canonicalFilePath() == QFileInfo(exe).canonicalFilePath())
        return onPath;
    return exe;
}

QJsonObject readCommands(QSettings &config)
{
    const QVariant v = config.value(CommandsKey);
    const QByteArray json = v.metaType() == QMetaType::fromType<QByteArray>() ? v.toByteArray() : v.toString().toUtf8();
    return QJsonDocument::fromJson(json.isEmpty() ? QByteArrayLiteral("{}") : json).object();
}

} // namespace

QString configPath(const QString &deviceId)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kdeconnect/") + deviceId + QLatin1Char('/') + PluginId
        + QStringLiteral("/config");
}

QString commandFor(const QString &actionId, const QString &deviceId)
{
    return shellQuote(launcher()) + QStringLiteral(" --run-action ") + shellQuote(actionId) + QStringLiteral(" --device ") + shellQuote(deviceId);
}

bool sync(const QString &deviceId, const QList<Entry> &entries)
{
    if (deviceId.isEmpty())
        return false;
    const QString path = configPath(deviceId);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qCWarning(lcRunCommand) << "can't create" << QFileInfo(path).absolutePath();
        return false;
    }

    QSettings config(path, QSettings::IniFormat);
    const QJsonObject before = readCommands(config);
    QJsonObject after;
    for (auto it = before.begin(); it != before.end(); ++it)
        if (!it.key().startsWith(KeyPrefix))
            after.insert(it.key(), it.value());
    for (const Entry &e : entries)
        after.insert(KeyPrefix + e.actionId, QJsonObject{{QStringLiteral("name"), e.name}, {QStringLiteral("command"), commandFor(e.actionId, deviceId)}});
    if (after == before)
        return true;

    config.setValue(CommandsKey, QJsonDocument(after).toJson(QJsonDocument::Compact));
    config.sync();
    if (config.status() != QSettings::NoError) {
        qCWarning(lcRunCommand) << "can't write" << path;
        return false;
    }

    // kdeconnectd rereads the file and sends the updated list to the phone.
    const auto signal = QDBusMessage::createSignal(QStringLiteral("/kdeconnect/") + deviceId + QLatin1Char('/') + PluginId,
                                                   QStringLiteral("org.kde.kdeconnect.config"), QStringLiteral("configChanged"));
    kdc::bus().send(signal);
    qCInfo(lcRunCommand) << "registered" << entries.size() << "actions with" << deviceId;
    return true;
}

int foreignCount(const QString &deviceId)
{
    if (deviceId.isEmpty() || !QFileInfo::exists(configPath(deviceId)))
        return 0;
    QSettings config(configPath(deviceId), QSettings::IniFormat);
    int n = 0;
    const QJsonObject commands = readCommands(config);
    for (auto it = commands.begin(); it != commands.end(); ++it)
        n += !it.key().startsWith(KeyPrefix);
    return n;
}

} // namespace runcommand
