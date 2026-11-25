#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class ActionRun;
class DeviceManager;

// User-built actions: named node graphs, saved to actions.json and offered to every
// paired phone as KDE Connect "Run command" entries (see runcommand.h).
class ActionStore : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.actions")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // Grows when a phone reports its nodes (SlonnectLink).
    Q_PROPERTY(QVariantList nodeTypes READ nodeTypes NOTIFY nodeTypesChanged)
    // False in demo and headless mode: KDE Connect's command lists are left alone.
    Q_PROPERTY(bool registering READ registering CONSTANT)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        EnabledRole,
        NodeCountRole,
        RunningRole,
        LastRunRole,
        LastOkRole,
        LastErrorRole,
    };

    explicit ActionStore(DeviceManager *devices, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantList nodeTypes() const;
    bool registering() const { return m_registering; }
    void setRegistering(bool on);

    Q_INVOKABLE QString create(const QString &name);
    // A ready-made action: "swap-earbuds" moves Bluetooth earbuds between this computer and the phone.
    Q_INVOKABLE QString createFromTemplate(const QString &templateId);
    Q_INVOKABLE QString duplicate(const QString &id);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void rename(const QString &id, const QString &name);
    Q_INVOKABLE void setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE int indexOf(const QString &id) const;
    Q_INVOKABLE QString idAt(int row) const { return row >= 0 && row < m_actions.size() ? m_actions.at(row).id : QString(); }
    Q_INVOKABLE QString name(const QString &id) const;
    Q_INVOKABLE QVariantMap graph(const QString &id) const;
    Q_INVOKABLE void setGraph(const QString &id, const QVariantMap &graph);
    Q_INVOKABLE QStringList log(const QString &id) const;
    // Run from the UI against `deviceId` (empty: no phone; phone nodes fail).
    Q_INVOKABLE void run(const QString &id, const QString &deviceId);
    Q_INVOKABLE void cancel(const QString &id);
    Q_INVOKABLE bool isRunning(const QString &id) const;
    // The shell command registered with KDE Connect, for display.
    Q_INVOKABLE QString commandFor(const QString &id, const QString &deviceId) const;
    // Commands on the device that weren't made by Slonnect.
    Q_INVOKABLE int otherCommands(const QString &deviceId) const;

    bool busy() const;

Q_SIGNALS:
    void countChanged();
    void nodeTypesChanged();
    void runStarted(const QString &actionId);
    void nodeState(const QString &actionId, const QString &nodeUid, const QString &state);
    void logged(const QString &actionId, const QString &line);
    void runFinished(const QString &actionId, bool ok, const QString &error);
    // No runs left (headless mode quits on this).
    void idle();

private:
    struct Action {
        QString id;
        QString name;
        bool enabled = true;
        QVariantMap graph;
        qint64 lastRun = 0;
        bool lastOk = true;
        QString lastError;
        QStringList log;
        QPointer<ActionRun> run;
    };

    Action *find(const QString &id);
    const Action *find(const QString &id) const;
    void changed(const QString &id, const QList<int> &roles = {});
    void load();
    void save() const;
    void scheduleSync();
    void syncCommands();
    void watchDevices();
    QString uniqueName(const QString &base) const;
    static QVariantMap defaultGraph(const QString &name);

    DeviceManager *m_devices;
    QList<Action> m_actions;
    QTimer m_syncTimer;
    bool m_registering = false;
};
