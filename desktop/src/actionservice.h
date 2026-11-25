#pragma once

#include <QObject>
#include <QStringList>

class ActionStore;

// Session-bus endpoint the KDE Connect commands reach: `slonnect --run-action <id>`
// calls Run() on whichever Slonnect owns dev.slonnect.Slonnect, so the action runs
// inside the open window (with live status in the editor).
//
//   busctl --user call dev.slonnect.Slonnect /Actions dev.slonnect.Actions Run ss <id> <device>
class ActionService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "dev.slonnect.Actions")

public:
    static inline const QString ServiceName = QStringLiteral("dev.slonnect.Slonnect");
    static inline const QString ObjectPath = QStringLiteral("/Actions");

    explicit ActionService(ActionStore *store, QObject *parent = nullptr);

    // queue: wait in line for the name (the GUI does, so it takes over from a
    // headless run once that exits). Returns true when we own the name now.
    bool registerOnBus(bool queue);

    // Ask a running instance to run the action. False when nobody answered.
    static bool forward(const QString &actionId, const QString &deviceId);

public Q_SLOTS:
    Q_SCRIPTABLE void Run(const QString &actionId, const QString &deviceId);
    // "id<TAB>name" per action.
    Q_SCRIPTABLE QStringList List() const;

private:
    ActionStore *m_store;
};

// dev.slonnect.App on the same bus name: a second `slonnect ...` launch hands its
// command line to the running window and exits.
//
//   busctl --user call dev.slonnect.Slonnect /App dev.slonnect.App Activate as 2 --page device
class AppService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "dev.slonnect.App")

public:
    static inline const QString ObjectPath = QStringLiteral("/App");

    using QObject::QObject;
    bool registerObject();
    // Hand `args` to a running Slonnect window. False when there is none.
    static bool forward(const QStringList &args);

public Q_SLOTS:
    Q_SCRIPTABLE void Activate(const QStringList &args);
};
