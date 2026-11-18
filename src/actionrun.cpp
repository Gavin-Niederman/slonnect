#include "actionrun.h"
#include "kdeconnect.h"
#include "nodetypes.h"

#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QLoggingCategory>
#include <QTime>
#include <QTimer>

Q_LOGGING_CATEGORY(lcActions, "slonnect.actions")

ActionRun::ActionRun(const QString &actionId, const QVariantMap &graph, const QString &deviceId, QObject *parent)
    : QObject(parent)
    , m_actionId(actionId)
    , m_deviceId(deviceId)
{
    for (const QVariant &v : graph.value(QStringLiteral("nodes")).toList()) {
        const QVariantMap n = v.toMap();
        Node node{n.value(QStringLiteral("id")).toString(), nodes::find(n.value(QStringLiteral("type")).toString()),
                  n.value(QStringLiteral("values")).toMap()};
        if (!node.uid.isEmpty())
            m_nodes.insert(node.uid, node);
    }
    for (const QVariant &v : graph.value(QStringLiteral("links")).toList()) {
        const QVariantMap l = v.toMap();
        m_links << Link{l.value(QStringLiteral("from")).toString(), l.value(QStringLiteral("fromPin")).toString(), l.value(QStringLiteral("to")).toString(),
                        l.value(QStringLiteral("toPin")).toString()};
    }
}

void ActionRun::start()
{
    if (m_deviceId.isEmpty())
        return begin();
    // Look up the phone's name so the trigger can hand it to the graph.
    auto msg = QDBusMessage::createMethodCall(kdc::Service, kdc::devicePath(m_deviceId), QStringLiteral("org.freedesktop.DBus.Properties"),
                                              QStringLiteral("Get"));
    msg.setArguments({kdc::DeviceIface, QStringLiteral("name")});
    auto *w = new QDBusPendingCallWatcher(kdc::bus().asyncCall(msg, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (!w->isError())
            m_deviceName = kdc::unwrap(w->reply().arguments().value(0)).toString();
        begin();
    });
}

void ActionRun::begin()
{
    QString trigger;
    for (const Node &n : std::as_const(m_nodes))
        if (n.type && n.type->trigger)
            trigger = n.uid;
    if (trigger.isEmpty())
        return finish(false, tr("This action has no Phone command trigger"));

    m_outputs[trigger] = {{QStringLiteral("device"), m_deviceName}, {QStringLiteral("deviceId"), m_deviceId}};
    log(trigger, m_deviceId.isEmpty() ? tr("Started without a phone") : tr("Started from %1").arg(m_deviceName.isEmpty() ? m_deviceId : m_deviceName));
    Q_EMIT nodeState(trigger, QStringLiteral("done"));
    m_pending << qMakePair(trigger, QStringLiteral("out"));
    step();
}

void ActionRun::cancel()
{
    finish(false, tr("Cancelled"));
}

void ActionRun::step()
{
    while (!m_finished && !m_pending.isEmpty()) {
        const auto [uid, pin] = m_pending.takeLast();
        for (const Link &l : std::as_const(m_links)) {
            if (l.from != uid || l.fromPin != pin)
                continue;
            const auto it = m_nodes.constFind(l.to);
            if (it == m_nodes.cend() || !it->type || it->type->isPure())
                continue;
            return execute(*it);
        }
    }
    if (!m_finished)
        finish(true);
}

void ActionRun::execute(const Node &node)
{
    if (++m_steps > MaxSteps)
        return finish(false, tr("Stopped after %1 steps; is there a loop?").arg(MaxSteps));

    QString error;
    const QVariantMap inputs = resolveInputs(node, 0, &error);
    if (!error.isEmpty()) {
        Q_EMIT nodeState(node.uid, QStringLiteral("failed"));
        return finish(false, title(node.uid) + QStringLiteral(": ") + error);
    }

    Q_EMIT nodeState(node.uid, QStringLiteral("running"));
    const quint64 token = ++m_token;
    const QString uid = node.uid;

    nodes::Ctx ctx;
    ctx.run = this;
    ctx.nodeUid = uid;
    ctx.in = inputs;
    QPointer<ActionRun> self(this);
    ctx.done = [self, token, uid](const QStringList &flows, const QVariantMap &outputs) {
        if (!self || self->m_finished || self->m_token != token)
            return;
        ++self->m_token; // a node completes once
        self->m_outputs[uid] = outputs;
        Q_EMIT self->nodeState(uid, QStringLiteral("done"));
        for (auto it = flows.crbegin(); it != flows.crend(); ++it)
            self->m_pending << qMakePair(uid, *it);
        // Always continue from the event loop, so synchronous nodes don't recurse.
        QTimer::singleShot(0, self, &ActionRun::step);
    };
    ctx.fail = [self, token, uid](const QString &err) {
        if (!self || self->m_finished || self->m_token != token)
            return;
        ++self->m_token;
        Q_EMIT self->nodeState(uid, QStringLiteral("failed"));
        self->finish(false, self->title(uid) + QStringLiteral(": ") + err);
    };
    node.type->action(ctx);
}

QVariantMap ActionRun::resolveInputs(const Node &node, int depth, QString *error)
{
    QVariantMap in;
    for (const auto &pin : node.type->inputs) {
        if (pin.kind == nodes::PinKind::Flow)
            continue;
        in.insert(pin.id, resolve(node, pin.id, depth, error));
        if (!error->isEmpty())
            break;
    }
    return in;
}

QVariant ActionRun::resolve(const Node &node, const QString &pin, int depth, QString *error)
{
    if (depth > 64) {
        *error = tr("Inputs are connected in a circle");
        return {};
    }
    for (const Link &l : std::as_const(m_links)) {
        if (l.to != node.uid || l.toPin != pin)
            continue;
        const auto src = m_nodes.constFind(l.from);
        if (src == m_nodes.cend() || !src->type)
            break;
        if (src->type->isPure()) {
            const QVariantMap in = resolveInputs(*src, depth + 1, error);
            return error->isEmpty() ? src->type->pure(in).value(l.fromPin) : QVariant();
        }
        // A flow node's outputs exist once it has run in this execution.
        const auto out = m_outputs.constFind(src->uid);
        if (out == m_outputs.cend()) {
            log(node.uid, tr("%1 hasn't run yet, so its output is empty").arg(title(src->uid)));
            return {};
        }
        return out->value(l.fromPin);
    }
    const auto *def = node.type->input(pin);
    return node.values.contains(pin) ? node.values.value(pin) : def ? def->defaultValue : QVariant();
}

QString ActionRun::title(const QString &uid) const
{
    const auto it = m_nodes.constFind(uid);
    return it != m_nodes.cend() && it->type ? it->type->title : uid;
}

void ActionRun::log(const QString &nodeUid, const QString &message, bool error)
{
    const QString line = QTime::currentTime().toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ")
        + (nodeUid.isEmpty() ? QString() : title(nodeUid) + QStringLiteral(" · ")) + (error ? QStringLiteral("✗ ") : QString()) + message;
    m_log << line;
    qCInfo(lcActions).noquote() << m_actionId << line;
    Q_EMIT logged(line);
}

void ActionRun::finish(bool ok, const QString &error)
{
    if (m_finished)
        return;
    m_finished = true;
    ++m_token;
    if (ok)
        log({}, tr("Finished"));
    else
        log({}, error, true);
    Q_EMIT finished(ok, error);
}
