// Registers Slonnect actions in KDE Connect's "Run command" plugin.
//
// kdeconnectd keeps the commands it offers to each phone as a JSON object in
// ~/.config/kdeconnect/<device>/kdeconnect_runcommand/config (QSettings INI, key
// "commands"): { "<key>": { "name": "...", "command": "..." } }. When the phone
// taps one, the daemon runs `/bin/sh -c <command>`. It rereads the file and pushes
// the new list to the phone when anything emits org.kde.kdeconnect.config
// configChanged on /kdeconnect/<device>/kdeconnect_runcommand.
//
// Slonnect owns the entries whose key starts with KeyPrefix. Each one runs
// `slonnect --run-action <id> --device <device>`, which hands the action to the
// running Slonnect over D-Bus (or runs it headless if Slonnect isn't open).
// Commands the user added in KDE Connect's own settings are left untouched.
#pragma once

#include <QList>
#include <QString>

namespace runcommand {

inline const QString PluginId = QStringLiteral("kdeconnect_runcommand");
inline const QString KeyPrefix = QStringLiteral("slonnect-");

struct Entry {
    QString actionId;
    QString name;
};

QString configPath(const QString &deviceId);

// The command line KDE Connect should run for an action.
QString commandFor(const QString &actionId, const QString &deviceId);

// Make Slonnect's entries for `deviceId` exactly `entries`. Returns false on I/O errors.
bool sync(const QString &deviceId, const QList<Entry> &entries);

// Number of commands on the device that Slonnect didn't create.
int foreignCount(const QString &deviceId);

} // namespace runcommand
