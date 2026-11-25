// The desktop end of the Slonnect protocol (protocol/README.md), spoken through the
// `slonnect` kdeconnectd plugin (nix/kdeconnect-slonnect-plugin.patch), which only
// forwards kdeconnect.slonnect.* packets to and from D-Bus.
//
// - Turns the node catalogue each phone sends in its hello into action-editor nodes
//   ("mobile.<id>", category "On the phone") that execute on the phone, and keeps the last
//   catalogue on disk so graphs using them open while the phone is away.
// - Publishes the actions to the Slonnect app on the phone, runs them when the phone asks
//   and reports their progress back.
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <functional>

class ActionStore;
class DeviceManager;
class QDBusMessage;

class SlonnectLink : public QObject
{
    Q_OBJECT

public:
    static inline const QString PluginId = QStringLiteral("kdeconnect_slonnect");
    static inline const QString Iface = QStringLiteral("org.kde.kdeconnect.device.slonnect");
    static inline const QString NodePrefix = QStringLiteral("mobile.");

    SlonnectLink(DeviceManager *devices, ActionStore *actions, bool demo, QObject *parent = nullptr);
    static SlonnectLink *instance();

    struct NodeResult {
        bool ok = false;
        QStringList flows;
        QVariantMap outputs;
        QString error;
        QStringList log;
    };
    using ResultFn = std::function<void(const NodeResult &)>;

    // Run one of the phone's nodes (id without the "mobile." prefix). deviceId may be empty:
    // then the first reachable phone running Slonnect is used. `context` going away drops the reply.
    void runNode(const QString &deviceId, const QString &node, const QVariantMap &inputs, QObject *context, ResultFn onResult,
                 int timeoutMs = 30000);

    // Phones that have said hello (i.e. run the Slonnect app).
    QStringList phones() const { return m_hello.keys(); }

    // A headless `slonnect --run-action ... --run-id <id>` reports status for the phone's runId.
    void expectRun(const QString &actionId, const QString &deviceId, const QString &runId);

private Q_SLOTS:
    void onPacket(const QString &type, const QString &body, const QDBusMessage &msg);
    void onDeviceConnected(const QDBusMessage &msg);

private:
    QString pluginPath(const QString &deviceId) const;
    void send(const QString &deviceId, const QString &type, const QVariantMap &body);
    void greet(const QString &deviceId);
    void sendActions(const QString &deviceId);
    void publishActions();
    void handleHello(const QString &deviceId, const QVariantMap &body);
    void registerCatalogue(const QVariantList &nodes);
    void loadCache();
    void saveCache() const;
    void scanDevices();
    QString pickPhone(const QString &preferred) const;
    void reportStatus(const QString &actionId, const QString &state, const QString &error = {}, const QString &step = {});
    void registerBundledCatalogue();

    DeviceManager *m_devices;
    ActionStore *m_actions;
    bool m_demo;
    // deviceId -> last hello body
    QHash<QString, QVariantMap> m_hello;
    QSet<QString> m_greeted;
    QTimer m_publish;

    struct Pending {
        QString deviceId;
        QPointer<QObject> context;
        ResultFn onResult;
        QTimer *timer = nullptr;
    };
    QHash<QString, Pending> m_pending;
    quint64 m_nextRequest = 0;

    // actionId -> (deviceId, runId) for runs the phone started
    struct PhoneRun {
        QString deviceId;
        QString runId;
        QString step;
    };
    QHash<QString, PhoneRun> m_phoneRuns;
};
