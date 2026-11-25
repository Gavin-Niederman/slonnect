#include "actionstore.h"
#include "actionrun.h"
#include "devicemanager.h"
#include "nodetypes.h"
#include "runcommand.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

Q_DECLARE_LOGGING_CATEGORY(lcActions)

static QString storePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/actions.json");
}

ActionStore::ActionStore(DeviceManager *devices, QObject *parent)
    : QAbstractListModel(parent)
    , m_devices(devices)
{
    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(400);
    connect(&m_syncTimer, &QTimer::timeout, this, &ActionStore::syncCommands);
    connect(nodes::Registry::instance(), &nodes::Registry::changed, this, &ActionStore::nodeTypesChanged);
    load();
}

int ActionStore::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_actions.size());
}

QVariant ActionStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_actions.size())
        return {};
    const Action &a = m_actions.at(index.row());
    switch (role) {
    case IdRole:
        return a.id;
    case NameRole:
        return a.name;
    case EnabledRole:
        return a.enabled;
    case NodeCountRole:
        return int(a.graph.value(QStringLiteral("nodes")).toList().size());
    case RunningRole:
        return bool(a.run);
    case LastRunRole:
        return a.lastRun;
    case LastOkRole:
        return a.lastOk;
    case LastErrorRole:
        return a.lastError;
    }
    return {};
}

QHash<int, QByteArray> ActionStore::roleNames() const
{
    return {{IdRole, "actionId"},       {NameRole, "name"},       {EnabledRole, "onPhone"},     {NodeCountRole, "nodeCount"},
            {RunningRole, "running"},   {LastRunRole, "lastRun"}, {LastOkRole, "lastOk"},       {LastErrorRole, "lastError"}};
}

QVariantList ActionStore::nodeTypes() const
{
    return nodes::catalog();
}

void ActionStore::setRegistering(bool on)
{
    if (m_registering == on)
        return;
    m_registering = on;
    if (on) {
        watchDevices();
        scheduleSync();
    }
}

ActionStore::Action *ActionStore::find(const QString &id)
{
    for (auto &a : m_actions)
        if (a.id == id)
            return &a;
    return nullptr;
}

const ActionStore::Action *ActionStore::find(const QString &id) const
{
    return const_cast<ActionStore *>(this)->find(id);
}

int ActionStore::indexOf(const QString &id) const
{
    for (int i = 0; i < m_actions.size(); ++i)
        if (m_actions[i].id == id)
            return i;
    return -1;
}

void ActionStore::changed(const QString &id, const QList<int> &roles)
{
    const int row = indexOf(id);
    if (row >= 0)
        Q_EMIT dataChanged(index(row), index(row), roles);
}

QString ActionStore::uniqueName(const QString &base) const
{
    auto taken = [this](const QString &n) {
        return std::any_of(m_actions.cbegin(), m_actions.cend(), [&](const Action &a) { return a.name.compare(n, Qt::CaseInsensitive) == 0; });
    };
    if (!taken(base))
        return base;
    for (int i = 2;; ++i)
        if (const QString n = QStringLiteral("%1 %2").arg(base).arg(i); !taken(n))
            return n;
}

QVariantMap ActionStore::defaultGraph(const QString &name)
{
    // A trigger wired to a notification, so a new action does something visible.
    const QVariantMap trigger{{QStringLiteral("id"), QStringLiteral("trigger")},
                              {QStringLiteral("type"), nodes::TriggerType},
                              {QStringLiteral("x"), 60},
                              {QStringLiteral("y"), 120}};
    const QVariantMap notify{{QStringLiteral("id"), QStringLiteral("n1")},
                             {QStringLiteral("type"), QStringLiteral("desktop.notify")},
                             {QStringLiteral("x"), 360},
                             {QStringLiteral("y"), 120},
                             {QStringLiteral("values"),
                              QVariantMap{{QStringLiteral("title"), name}, {QStringLiteral("body"), tr("Triggered from the phone")}}}};
    const QVariantMap link{{QStringLiteral("from"), QStringLiteral("trigger")},
                           {QStringLiteral("fromPin"), QStringLiteral("out")},
                           {QStringLiteral("to"), QStringLiteral("n1")},
                           {QStringLiteral("toPin"), QStringLiteral("in")}};
    return {{QStringLiteral("nodes"), QVariantList{trigger, notify}}, {QStringLiteral("links"), QVariantList{link}}};
}

QString ActionStore::create(const QString &name)
{
    Action a;
    a.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    a.name = uniqueName(name.trimmed().isEmpty() ? tr("New action") : name.trimmed());
    a.graph = defaultGraph(a.name);
    beginInsertRows({}, int(m_actions.size()), int(m_actions.size()));
    m_actions << a;
    endInsertRows();
    Q_EMIT countChanged();
    save();
    scheduleSync();
    return a.id;
}

QString ActionStore::duplicate(const QString &id)
{
    const Action *src = find(id);
    if (!src)
        return {};
    Action a;
    a.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    a.name = uniqueName(tr("%1 copy").arg(src->name));
    a.enabled = src->enabled;
    a.graph = src->graph;
    const int row = indexOf(id) + 1;
    beginInsertRows({}, row, row);
    m_actions.insert(row, a);
    endInsertRows();
    Q_EMIT countChanged();
    save();
    scheduleSync();
    return a.id;
}

void ActionStore::remove(const QString &id)
{
    const int row = indexOf(id);
    if (row < 0)
        return;
    if (m_actions[row].run)
        m_actions[row].run->cancel();
    beginRemoveRows({}, row, row);
    m_actions.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
    save();
    scheduleSync();
}

void ActionStore::rename(const QString &id, const QString &name)
{
    Action *a = find(id);
    const QString trimmed = name.trimmed();
    if (!a || trimmed.isEmpty() || trimmed == a->name)
        return;
    a->name = trimmed;
    changed(id, {NameRole});
    save();
    scheduleSync();
}

void ActionStore::setEnabled(const QString &id, bool enabled)
{
    Action *a = find(id);
    if (!a || a->enabled == enabled)
        return;
    a->enabled = enabled;
    changed(id, {EnabledRole});
    save();
    scheduleSync();
}

QString ActionStore::name(const QString &id) const
{
    const Action *a = find(id);
    return a ? a->name : QString();
}

QVariantMap ActionStore::graph(const QString &id) const
{
    const Action *a = find(id);
    return a ? a->graph : QVariantMap();
}

void ActionStore::setGraph(const QString &id, const QVariantMap &graph)
{
    Action *a = find(id);
    if (!a || a->graph == graph)
        return;
    a->graph = graph;
    changed(id, {NodeCountRole});
    save();
}

QStringList ActionStore::log(const QString &id) const
{
    const Action *a = find(id);
    return a ? a->log : QStringList();
}

bool ActionStore::isRunning(const QString &id) const
{
    const Action *a = find(id);
    return a && a->run;
}

bool ActionStore::busy() const
{
    return std::any_of(m_actions.cbegin(), m_actions.cend(), [](const Action &a) { return bool(a.run); });
}

void ActionStore::run(const QString &id, const QString &deviceId)
{
    Action *a = find(id);
    if (!a) {
        qCWarning(lcActions) << "no action with id" << id;
        if (!busy())
            Q_EMIT idle();
        return;
    }
    if (a->run) {
        // A second tap while it's still going (e.g. inside a Wait) is ignored.
        a->run->log({}, tr("Already running; ignored another trigger"));
        return;
    }
    auto *r = new ActionRun(id, a->graph, deviceId, this);
    a->run = r;
    a->log.clear();
    a->lastRun = QDateTime::currentMSecsSinceEpoch();
    connect(r, &ActionRun::nodeState, this, [this, id](const QString &uid, const QString &state) { Q_EMIT nodeState(id, uid, state); });
    connect(r, &ActionRun::logged, this, [this, id](const QString &line) {
        if (Action *a = find(id))
            a->log << line;
        Q_EMIT logged(id, line);
    });
    connect(r, &ActionRun::finished, this, [this, id, r](bool ok, const QString &error) {
        r->deleteLater();
        if (Action *a = find(id)) {
            a->run = nullptr;
            a->lastOk = ok;
            a->lastError = error;
            changed(id, {RunningRole, LastOkRole, LastErrorRole, LastRunRole});
        }
        Q_EMIT runFinished(id, ok, error);
        if (!busy())
            Q_EMIT idle();
    });
    changed(id, {RunningRole, LastRunRole});
    Q_EMIT runStarted(id);
    r->start();
}

void ActionStore::cancel(const QString &id)
{
    if (Action *a = find(id); a && a->run)
        a->run->cancel();
}

QString ActionStore::commandFor(const QString &id, const QString &deviceId) const
{
    return runcommand::commandFor(id, deviceId);
}

int ActionStore::otherCommands(const QString &deviceId) const
{
    return runcommand::foreignCount(deviceId);
}

// --- persistence -------------------------------------------------------------------

void ActionStore::load()
{
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray list = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("actions")).toArray();
    for (const auto &v : list) {
        const QJsonObject o = v.toObject();
        Action a;
        a.id = o.value(QStringLiteral("id")).toString();
        a.name = o.value(QStringLiteral("name")).toString();
        a.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        a.graph = o.value(QStringLiteral("graph")).toObject().toVariantMap();
        if (!a.id.isEmpty() && !find(a.id))
            m_actions << a;
    }
}

void ActionStore::save() const
{
    QJsonArray list;
    for (const Action &a : m_actions)
        list << QJsonObject{{QStringLiteral("id"), a.id},
                            {QStringLiteral("name"), a.name},
                            {QStringLiteral("enabled"), a.enabled},
                            {QStringLiteral("graph"), QJsonObject::fromVariantMap(a.graph)}};
    QDir().mkpath(QFileInfo(storePath()).absolutePath());
    QSaveFile f(storePath());
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(lcActions) << "can't write" << storePath();
        return;
    }
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("actions"), list}}).toJson());
    f.commit();
}

// --- KDE Connect registration ------------------------------------------------------

void ActionStore::watchDevices()
{
    connect(m_devices, &QAbstractItemModel::rowsInserted, this, &ActionStore::scheduleSync);
    connect(m_devices, &QAbstractItemModel::modelReset, this, &ActionStore::scheduleSync);
    // Pairing state arrives after the row does.
    connect(m_devices, &QAbstractItemModel::dataChanged, this, &ActionStore::scheduleSync);
}

void ActionStore::scheduleSync()
{
    if (m_registering)
        m_syncTimer.start();
}

void ActionStore::syncCommands()
{
    QList<runcommand::Entry> entries;
    for (const Action &a : std::as_const(m_actions))
        if (a.enabled)
            entries << runcommand::Entry{a.id, a.name};
    for (int i = 0; i < m_devices->rowCount(); ++i) {
        const QModelIndex idx = m_devices->index(i);
        if (idx.data(DeviceManager::PairedRole).toBool())
            runcommand::sync(idx.data(DeviceManager::IdRole).toString(), entries);
    }
}

namespace {
QVariantMap node(const QString &id, const QString &type, int x, int y, const QVariantMap &values = {})
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("type"), type}, {QStringLiteral("x"), x}, {QStringLiteral("y"), y}, {QStringLiteral("values"), values}};
}
QVariantMap wire(const QString &from, const QString &fromPin, const QString &to, const QString &toPin = QStringLiteral("in"))
{
    return {{QStringLiteral("from"), from}, {QStringLiteral("fromPin"), fromPin}, {QStringLiteral("to"), to}, {QStringLiteral("toPin"), toPin}};
}
} // namespace

QString ActionStore::createFromTemplate(const QString &templateId)
{
    if (templateId != QLatin1String("swap-earbuds"))
        return create(templateId);

    // Whichever side has the earbuds lets go first, then the other one connects, so it also
    // works with earbuds that only take one connection at a time. The name lives in one Text
    // node feeding every Bluetooth node.
    const QString bt = QStringLiteral("desktop.bluetooth");
    const QString phoneBt = QStringLiteral("mobile.bluetooth");
    const QString s = QStringLiteral("status"), conn = QStringLiteral("connect"), disc = QStringLiteral("disconnect");
    QVariantList nodes{
        node(QStringLiteral("trigger"), nodes::TriggerType, 40, 160),
        node(QStringLiteral("name"), QStringLiteral("data.text"), 40, 360, {{QStringLiteral("value"), tr("My earbuds")}}),
        node(QStringLiteral("check"), bt, 320, 120, {{QStringLiteral("action"), s}}),
        node(QStringLiteral("if"), QStringLiteral("logic.if"), 640, 120, {{QStringLiteral("op"), QStringLiteral("=")}, {QStringLiteral("b"), QStringLiteral("true")}}),
        // Here → phone
        node(QStringLiteral("deskOff"), bt, 940, 20, {{QStringLiteral("action"), disc}}),
        node(QStringLiteral("phoneOn"), phoneBt, 1260, 20, {{QStringLiteral("mode"), conn}}),
        // Phone → here
        node(QStringLiteral("phoneOff"), phoneBt, 940, 300, {{QStringLiteral("mode"), disc}}),
        node(QStringLiteral("deskOn"), bt, 1260, 300, {{QStringLiteral("action"), conn}}),
        node(QStringLiteral("toPhone"), QStringLiteral("desktop.notify"), 1580, 20,
             {{QStringLiteral("title"), tr("Earbuds")}, {QStringLiteral("body"), tr("Moved to the phone")}}),
        node(QStringLiteral("toDesk"), QStringLiteral("desktop.notify"), 1580, 300,
             {{QStringLiteral("title"), tr("Earbuds")}, {QStringLiteral("body"), tr("Moved to this computer")}}),
    };
    QVariantList links{
        wire(QStringLiteral("trigger"), QStringLiteral("out"), QStringLiteral("check")),
        wire(QStringLiteral("check"), QStringLiteral("out"), QStringLiteral("if")),
        wire(QStringLiteral("check"), QStringLiteral("connected"), QStringLiteral("if"), QStringLiteral("a")),
        wire(QStringLiteral("if"), QStringLiteral("true"), QStringLiteral("deskOff")),
        wire(QStringLiteral("deskOff"), QStringLiteral("out"), QStringLiteral("phoneOn")),
        wire(QStringLiteral("phoneOn"), QStringLiteral("out"), QStringLiteral("toPhone")),
        wire(QStringLiteral("if"), QStringLiteral("false"), QStringLiteral("phoneOff")),
        wire(QStringLiteral("phoneOff"), QStringLiteral("out"), QStringLiteral("deskOn")),
        wire(QStringLiteral("deskOn"), QStringLiteral("out"), QStringLiteral("toDesk")),
    };
    for (const QString &target : {QStringLiteral("check"), QStringLiteral("deskOff"), QStringLiteral("phoneOn"), QStringLiteral("phoneOff"), QStringLiteral("deskOn")})
        links << wire(QStringLiteral("name"), QStringLiteral("out"), target, QStringLiteral("device"));

    const QString id = create(tr("Swap earbuds"));
    setGraph(id, {{QStringLiteral("nodes"), nodes}, {QStringLiteral("links"), links}});
    return id;
}
