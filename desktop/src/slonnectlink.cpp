#include "slonnectlink.h"
#include "actionrun.h"
#include "actionstore.h"
#include "devicemanager.h"
#include "kdeconnect.h"
#include "nodes_p.h"
#include "nodetypes.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

Q_LOGGING_CATEGORY(lcLink, "slonnect.link")

namespace {
const QString Hello = QStringLiteral("kdeconnect.slonnect.hello");
const QString NodeRun = QStringLiteral("kdeconnect.slonnect.node.run");
const QString NodeResultType = QStringLiteral("kdeconnect.slonnect.node.result");
const QString Actions = QStringLiteral("kdeconnect.slonnect.actions");
const QString ActionsRequest = QStringLiteral("kdeconnect.slonnect.actions.request");
const QString ActionRunType = QStringLiteral("kdeconnect.slonnect.action.run");
const QString ActionStatus = QStringLiteral("kdeconnect.slonnect.action.status");
constexpr int ProtocolVersion = 1;

SlonnectLink *s_instance = nullptr;

QString cachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/phone-nodes.json");
}

QString toJson(const QVariantMap &body)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(body)).toJson(QJsonDocument::Compact));
}

QVariantMap fromJson(const QString &body)
{
    return QJsonDocument::fromJson(body.toUtf8()).object().toVariantMap();
}

QString deviceFromPath(const QString &path)
{
    // /modules/kdeconnect/devices/<id>/slonnect
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.size() >= 4 ? parts.at(3) : QString();
}

nodes::PinKind pinKind(const QString &kind)
{
    if (kind == QLatin1String("number"))
        return nodes::PinKind::Number;
    if (kind == QLatin1String("bool"))
        return nodes::PinKind::Bool;
    if (kind == QLatin1String("choice"))
        return nodes::PinKind::Choice;
    return nodes::PinKind::Text;
}

nodes::PinDef pinFrom(const QVariantMap &p)
{
    nodes::PinDef pin;
    pin.id = p.value(QStringLiteral("id")).toString();
    pin.label = p.value(QStringLiteral("label"), pin.id).toString();
    pin.kind = pinKind(p.value(QStringLiteral("kind")).toString());
    pin.defaultValue = p.value(QStringLiteral("default"));
    pin.options = p.value(QStringLiteral("options")).toStringList();
    pin.placeholder = p.value(QStringLiteral("placeholder")).toString();
    return pin;
}
} // namespace

SlonnectLink::SlonnectLink(DeviceManager *devices, ActionStore *actions, bool demo, QObject *parent)
    : QObject(parent)
    , m_devices(devices)
    , m_actions(actions)
    , m_demo(demo)
{
    s_instance = this;
    m_publish.setSingleShot(true);
    m_publish.setInterval(500);
    connect(&m_publish, &QTimer::timeout, this, &SlonnectLink::publishActions);

    // The bundled catalogue first, so graphs with phone nodes load (and keep them when saved)
    // before any phone has described its nodes; a phone's hello or the cache replace them.
    registerBundledCatalogue();
    if (m_demo)
        return;
    loadCache();

    // Every device's plugin object, so phones that connect later are covered too.
    auto bus = kdc::bus();
    bus.connect(kdc::Service, QString(), Iface, QStringLiteral("packetReceived"), this, SLOT(onPacket(QString, QString, QDBusMessage)));
    bus.connect(kdc::Service, QString(), Iface, QStringLiteral("deviceConnected"), this, SLOT(onDeviceConnected(QDBusMessage)));

    connect(m_devices, &QAbstractItemModel::rowsInserted, this, &SlonnectLink::scanDevices);
    connect(m_devices, &QAbstractItemModel::modelReset, this, &SlonnectLink::scanDevices);
    connect(m_devices, &QAbstractItemModel::dataChanged, this, &SlonnectLink::scanDevices);
    QTimer::singleShot(0, this, &SlonnectLink::scanDevices);

    // Actions edited, added or removed: tell the phones.
    connect(m_actions, &QAbstractItemModel::dataChanged, &m_publish, qOverload<>(&QTimer::start));
    connect(m_actions, &QAbstractItemModel::rowsInserted, &m_publish, qOverload<>(&QTimer::start));
    connect(m_actions, &QAbstractItemModel::rowsRemoved, &m_publish, qOverload<>(&QTimer::start));
    connect(m_actions, &QAbstractItemModel::modelReset, &m_publish, qOverload<>(&QTimer::start));

    // Progress of runs the phone asked for.
    connect(m_actions, &ActionStore::runStarted, this, [this](const QString &id) { reportStatus(id, QStringLiteral("running")); });
    connect(m_actions, &ActionStore::nodeState, this, [this](const QString &id, const QString &uid, const QString &state) {
        auto it = m_phoneRuns.find(id);
        if (it == m_phoneRuns.end() || state != QLatin1String("running"))
            return;
        const auto graphNodes = m_actions->graph(id).value(QStringLiteral("nodes")).toList();
        for (const QVariant &n : graphNodes) {
            const QVariantMap node = n.toMap();
            if (node.value(QStringLiteral("id")).toString() == uid) {
                const auto *type = nodes::find(node.value(QStringLiteral("type")).toString());
                reportStatus(id, QStringLiteral("running"), {}, type ? type->title : uid);
            }
        }
    });
    connect(m_actions, &ActionStore::runFinished, this, [this](const QString &id, bool ok, const QString &error) {
        reportStatus(id, ok ? QStringLiteral("done") : QStringLiteral("failed"), error);
        m_phoneRuns.remove(id);
    });
}

SlonnectLink *SlonnectLink::instance()
{
    return s_instance;
}

QString SlonnectLink::pluginPath(const QString &deviceId) const
{
    return kdc::devicePath(deviceId) + QStringLiteral("/slonnect");
}

void SlonnectLink::send(const QString &deviceId, const QString &type, const QVariantMap &body)
{
    if (m_demo || deviceId.isEmpty())
        return;
    auto msg = QDBusMessage::createMethodCall(kdc::Service, pluginPath(deviceId), Iface, QStringLiteral("sendPacket"));
    msg.setArguments({type, toJson(body)});
    auto *w = new QDBusPendingCallWatcher(kdc::bus().asyncCall(msg, 5000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [type, deviceId](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError())
            qCDebug(lcLink) << "sending" << type << "to" << deviceId << "failed:" << w->error().message();
    });
}

// Devices whose slonnect plugin is loaded (i.e. the phone runs Slonnect) get a hello once
// per connection; we also pick up a hello that arrived while we weren't running.
void SlonnectLink::scanDevices()
{
    QSet<QString> reachable;
    for (int i = 0; i < m_devices->rowCount(); ++i) {
        const QModelIndex idx = m_devices->index(i);
        const QString id = idx.data(DeviceManager::IdRole).toString();
        if (!idx.data(DeviceManager::ReachableRole).toBool() || !idx.data(DeviceManager::PairedRole).toBool())
            continue;
        const Device *device = m_devices->find(id);
        if (!device || !device->loadedPlugins().contains(PluginId))
            continue;
        reachable << id;
        if (m_greeted.contains(id))
            continue;
        m_greeted << id;

        auto msg = QDBusMessage::createMethodCall(kdc::Service, pluginPath(id), Iface, QStringLiteral("lastHello"));
        auto *w = new QDBusPendingCallWatcher(kdc::bus().asyncCall(msg, 3000), this);
        connect(w, &QDBusPendingCallWatcher::finished, this, [this, id](QDBusPendingCallWatcher *w) {
            w->deleteLater();
            const QString body = w->isError() ? QString() : w->reply().arguments().value(0).toString();
            if (!body.isEmpty())
                handleHello(id, fromJson(body));
            greet(id);
        });
    }
    // Forget devices that went away so they're greeted again when they're back.
    m_greeted.intersect(reachable);
}

void SlonnectLink::greet(const QString &deviceId)
{
    send(deviceId, Hello,
         {{QStringLiteral("version"), ProtocolVersion},
          {QStringLiteral("role"), QStringLiteral("desktop")},
          {QStringLiteral("app"), QStringLiteral("slonnect " SLONNECT_VERSION)},
          {QStringLiteral("features"), QStringList{QStringLiteral("actions"), QStringLiteral("nodes")}},
          {QStringLiteral("nodes"), QVariantList{}},
          {QStringLiteral("replyRequested"), true}});
    sendActions(deviceId);
}

void SlonnectLink::sendActions(const QString &deviceId)
{
    QVariantList list;
    for (int i = 0; i < m_actions->rowCount(); ++i) {
        const QModelIndex idx = m_actions->index(i);
        if (!idx.data(ActionStore::EnabledRole).toBool())
            continue;
        list << QVariantMap{{QStringLiteral("id"), idx.data(ActionStore::IdRole)},
                            {QStringLiteral("name"), idx.data(ActionStore::NameRole)},
                            {QStringLiteral("icon"), QStringLiteral("bolt")}};
    }
    send(deviceId, Actions, {{QStringLiteral("actions"), list}});
}

void SlonnectLink::publishActions()
{
    for (const QString &id : std::as_const(m_greeted))
        sendActions(id);
}

void SlonnectLink::onDeviceConnected(const QDBusMessage &msg)
{
    const QString id = deviceFromPath(msg.path());
    m_greeted.remove(id);
    QTimer::singleShot(0, this, &SlonnectLink::scanDevices);
}

void SlonnectLink::onPacket(const QString &type, const QString &body, const QDBusMessage &msg)
{
    const QString deviceId = deviceFromPath(msg.path());
    const QVariantMap b = fromJson(body);
    if (type == Hello) {
        handleHello(deviceId, b);
        if (b.value(QStringLiteral("replyRequested")).toBool())
            greet(deviceId);
    } else if (type == NodeResultType) {
        const QString requestId = b.value(QStringLiteral("requestId")).toString();
        auto it = m_pending.find(requestId);
        if (it == m_pending.end())
            return;
        Pending p = it.value();
        m_pending.erase(it);
        delete p.timer;
        if (!p.context)
            return;
        NodeResult r;
        r.ok = b.value(QStringLiteral("ok")).toBool();
        r.flows = b.value(QStringLiteral("flows"), QStringList{QStringLiteral("out")}).toStringList();
        r.outputs = b.value(QStringLiteral("outputs")).toMap();
        r.error = b.value(QStringLiteral("error")).toString();
        r.log = b.value(QStringLiteral("log")).toStringList();
        p.onResult(r);
    } else if (type == ActionsRequest) {
        sendActions(deviceId);
    } else if (type == ActionRunType) {
        const QString actionId = b.value(QStringLiteral("id")).toString();
        expectRun(actionId, deviceId, b.value(QStringLiteral("runId")).toString());
        if (m_actions->indexOf(actionId) < 0) {
            reportStatus(actionId, QStringLiteral("failed"), tr("This action no longer exists on the computer"));
            m_phoneRuns.remove(actionId);
            sendActions(deviceId);
            return;
        }
        if (m_actions->isRunning(actionId)) {
            reportStatus(actionId, QStringLiteral("running"));
            return;
        }
        m_actions->run(actionId, deviceId);
    }
}

void SlonnectLink::expectRun(const QString &actionId, const QString &deviceId, const QString &runId)
{
    m_phoneRuns.insert(actionId, {deviceId, runId, {}});
}

void SlonnectLink::reportStatus(const QString &actionId, const QString &state, const QString &error, const QString &step)
{
    const auto it = m_phoneRuns.constFind(actionId);
    if (it == m_phoneRuns.cend())
        return;
    QVariantMap body{{QStringLiteral("id"), actionId}, {QStringLiteral("runId"), it->runId}, {QStringLiteral("state"), state}};
    if (!error.isEmpty())
        body.insert(QStringLiteral("error"), error);
    if (!step.isEmpty())
        body.insert(QStringLiteral("step"), step);
    send(it->deviceId, ActionStatus, body);
}

void SlonnectLink::handleHello(const QString &deviceId, const QVariantMap &body)
{
    if (deviceId.isEmpty() || body.value(QStringLiteral("role")).toString() != QLatin1String("mobile"))
        return;
    m_hello.insert(deviceId, body);
    registerCatalogue(body.value(QStringLiteral("nodes")).toList());
    saveCache();
}

void SlonnectLink::registerCatalogue(const QVariantList &descriptors)
{
    for (const QVariant &v : descriptors) {
        const QVariantMap d = v.toMap();
        const QString remoteId = d.value(QStringLiteral("id")).toString();
        if (remoteId.isEmpty())
            continue;
        nodes::NodeType t;
        t.id = NodePrefix + remoteId;
        t.title = d.value(QStringLiteral("title"), remoteId).toString();
        t.category = tr("On the phone");
        t.icon = d.value(QStringLiteral("icon"), QStringLiteral("smartphone")).toString();
        t.description = d.value(QStringLiteral("description")).toString();
        t.remote = true;
        if (!m_demo)
            t.plugin = PluginId; // the editor warns when the selected phone lacks the Slonnect app
        if (!d.value(QStringLiteral("available"), true).toBool())
            t.unavailableReason = d.value(QStringLiteral("unavailableReason"), tr("Needs setup on the phone")).toString();
        t.inputs << nodes::detail::flowIn();
        for (const QVariant &p : d.value(QStringLiteral("inputs")).toList())
            t.inputs << pinFrom(p.toMap());
        QStringList flows = d.value(QStringLiteral("flows")).toStringList();
        if (flows.isEmpty())
            flows << QStringLiteral("out");
        for (const QString &f : std::as_const(flows))
            t.outputs << nodes::detail::flowOut(f, f == QLatin1String("out") ? QString() : f);
        for (const QVariant &p : d.value(QStringLiteral("outputs")).toList())
            t.outputs << pinFrom(p.toMap());
        t.width = 250;
        t.action = [remoteId](const nodes::Ctx &c) {
            auto *link = SlonnectLink::instance();
            if (!link)
                return c.fail(tr("Not connected to the phone"));
            link->runNode(c.deviceId(), remoteId, c.in, c.run.data(), [c](const NodeResult &r) {
                for (const QString &line : r.log)
                    c.log(line);
                if (!r.ok)
                    return c.fail(r.error.isEmpty() ? tr("Failed on the phone") : r.error);
                c.done(r.flows, r.outputs);
            });
        };
        nodes::registerExternal(t);
    }
}

QString SlonnectLink::pickPhone(const QString &preferred) const
{
    if (!preferred.isEmpty())
        return preferred;
    for (const QString &id : m_greeted)
        if (m_hello.contains(id))
            return id;
    return {};
}

void SlonnectLink::runNode(const QString &deviceId, const QString &node, const QVariantMap &inputs, QObject *context, ResultFn onResult,
                           int timeoutMs)
{
    if (m_demo) {
        // Demo: pretend the phone did it, a moment later.
        QTimer::singleShot(400, context, [onResult, node] {
            NodeResult r;
            r.ok = true;
            r.flows = {QStringLiteral("out")};
            r.log = {QStringLiteral("(demo) %1 ran on the phone").arg(node)};
            onResult(r);
        });
        return;
    }
    const QString phone = pickPhone(deviceId);
    if (phone.isEmpty() || !m_greeted.contains(phone)) {
        NodeResult r;
        r.error = phone.isEmpty() ? tr("No phone with the Slonnect app is connected") : tr("The phone isn't connected");
        QTimer::singleShot(0, context, [onResult, r] { onResult(r); });
        return;
    }
    const QString requestId = QStringLiteral("r-%1-%2").arg(QUuid::createUuid().toString(QUuid::Id128).left(8)).arg(++m_nextRequest);
    Pending p;
    p.deviceId = phone;
    p.context = context;
    p.onResult = std::move(onResult);
    p.timer = new QTimer(this);
    p.timer->setSingleShot(true);
    connect(p.timer, &QTimer::timeout, this, [this, requestId, timeoutMs] {
        auto it = m_pending.find(requestId);
        if (it == m_pending.end())
            return;
        Pending p = it.value();
        m_pending.erase(it);
        p.timer->deleteLater();
        if (!p.context)
            return;
        NodeResult r;
        r.error = tr("The phone didn't answer within %1 s").arg(timeoutMs / 1000);
        p.onResult(r);
    });
    p.timer->start(timeoutMs);
    m_pending.insert(requestId, p);
    send(phone, NodeRun,
         {{QStringLiteral("requestId"), requestId},
          {QStringLiteral("node"), node},
          {QStringLiteral("inputs"), inputs},
          {QStringLiteral("timeoutMs"), timeoutMs}});
}

void SlonnectLink::loadCache()
{
    QFile f(cachePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QVariantMap all = QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
    for (auto it = all.cbegin(); it != all.cend(); ++it)
        registerCatalogue(it.value().toMap().value(QStringLiteral("nodes")).toList());
}

void SlonnectLink::saveCache() const
{
    QVariantMap all;
    for (auto it = m_hello.cbegin(); it != m_hello.cend(); ++it)
        all.insert(it.key(), QVariantMap{{QStringLiteral("nodes"), it.value().value(QStringLiteral("nodes"))}});
    QDir().mkpath(QFileInfo(cachePath()).absolutePath());
    QSaveFile f(cachePath());
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(QJsonObject::fromVariantMap(all)).toJson());
    f.commit();
}

void SlonnectLink::registerBundledCatalogue()
{
    QFile f(QStringLiteral(":/qt/qml/Slonnect/data/phone-nodes.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcLink) << "bundled phone catalogue missing";
        return;
    }
    QVariantList nodes = QJsonDocument::fromJson(f.readAll()).object().toVariantMap().value(QStringLiteral("nodes")).toList();
    if (!m_demo) {
        for (QVariant &n : nodes) {
            QVariantMap m = n.toMap();
            m.insert(QStringLiteral("available"), false);
            m.insert(QStringLiteral("unavailableReason"), tr("Connect a phone running the Slonnect app"));
            n = m;
        }
    }
    registerCatalogue(nodes);
}
