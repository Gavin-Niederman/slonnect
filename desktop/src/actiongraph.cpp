#include "actiongraph.h"
#include "nodetypes.h"

#include <QRegularExpression>
#include <QSet>

ActionGraph::ActionGraph(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ActionGraph::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_nodes.size());
}

QVariant ActionGraph::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_nodes.size())
        return {};
    const Node &n = m_nodes.at(index.row());
    const nodes::NodeType &t = *n.type;
    switch (role) {
    case UidRole:
        return n.uid;
    case TypeRole:
        return t.id;
    case TitleRole:
        return t.title;
    case CategoryRole:
        return t.category;
    case IconRole:
        return t.icon;
    case DescriptionRole:
        return t.description;
    case PosXRole:
        return n.x;
    case PosYRole:
        return n.y;
    case WidthRole:
        return t.width;
    case InputsRole:
        return nodes::describePins(t.inputs);
    case OutputsRole:
        return nodes::describePins(t.outputs);
    case ValuesRole: {
        // Defaults filled in, so editors always have something to show.
        QVariantMap v;
        for (const auto &p : t.inputs)
            if (p.kind != nodes::PinKind::Flow)
                v.insert(p.id, n.values.contains(p.id) ? n.values.value(p.id) : p.defaultValue);
        return v;
    }
    case LinkedInputsRole: {
        QStringList pins;
        for (const Link &l : m_links)
            if (l.to == n.uid)
                pins << l.toPin;
        return pins;
    }
    case LinkedOutputsRole: {
        QStringList pins;
        for (const Link &l : m_links)
            if (l.from == n.uid)
                pins << l.fromPin;
        return pins;
    }
    case TriggerRole:
        return t.trigger;
    case PureRole:
        return t.isPure();
    case PluginRole:
        return t.plugin;
    case UnavailableRole:
        return t.unavailableReason;
    }
    return {};
}

QHash<int, QByteArray> ActionGraph::roleNames() const
{
    return {{UidRole, "uid"},
            {TypeRole, "nodeType"},
            {TitleRole, "title"},
            {CategoryRole, "category"},
            {IconRole, "iconName"},
            {DescriptionRole, "description"},
            {PosXRole, "posX"},
            {PosYRole, "posY"},
            {WidthRole, "nodeWidth"},
            {InputsRole, "inputs"},
            {OutputsRole, "outputs"},
            {ValuesRole, "values"},
            {LinkedInputsRole, "linkedInputs"},
            {LinkedOutputsRole, "linkedOutputs"},
            {TriggerRole, "isTrigger"},
            {PureRole, "isPure"},
            {PluginRole, "plugin"},
            {UnavailableRole, "unavailable"}};
}

QVariantList ActionGraph::links() const
{
    QVariantList list;
    for (const Link &l : m_links)
        list << QVariantMap{{QStringLiteral("from"), l.from},
                            {QStringLiteral("fromPin"), l.fromPin},
                            {QStringLiteral("to"), l.to},
                            {QStringLiteral("toPin"), l.toPin},
                            {QStringLiteral("flow"), l.flow}};
    return list;
}

int ActionGraph::rowOf(const QString &uid) const
{
    for (int i = 0; i < m_nodes.size(); ++i)
        if (m_nodes[i].uid == uid)
            return i;
    return -1;
}

const ActionGraph::Node *ActionGraph::node(const QString &uid) const
{
    const int row = rowOf(uid);
    return row < 0 ? nullptr : &m_nodes[row];
}

QString ActionGraph::nextUid()
{
    QString uid;
    do
        uid = QStringLiteral("n%1").arg(m_nextUid++);
    while (rowOf(uid) >= 0);
    return uid;
}

void ActionGraph::load(const QVariantMap &graph)
{
    beginResetModel();
    m_nodes.clear();
    m_links.clear();
    m_nextUid = 1;
    static const QRegularExpression numbered(QStringLiteral("^n(\\d+)$"));
    for (const QVariant &v : graph.value(QStringLiteral("nodes")).toList()) {
        const QVariantMap m = v.toMap();
        Node n{m.value(QStringLiteral("id")).toString(), nodes::find(m.value(QStringLiteral("type")).toString()), m.value(QStringLiteral("x")).toReal(),
               m.value(QStringLiteral("y")).toReal(), m.value(QStringLiteral("values")).toMap()};
        if (n.uid.isEmpty() || !n.type || rowOf(n.uid) >= 0)
            continue; // unknown node types (e.g. from a newer Slonnect) are dropped
        if (const auto match = numbered.match(n.uid); match.hasMatch())
            m_nextUid = std::max(m_nextUid, match.captured(1).toInt() + 1);
        m_nodes << n;
    }
    // Every action has exactly one trigger.
    if (std::none_of(m_nodes.cbegin(), m_nodes.cend(), [](const Node &n) { return n.type->trigger; }))
        m_nodes.prepend(Node{QStringLiteral("trigger"), nodes::find(nodes::TriggerType), 60, 120, {}});
    for (const QVariant &v : graph.value(QStringLiteral("links")).toList()) {
        const QVariantMap m = v.toMap();
        const QString from = m.value(QStringLiteral("from")).toString(), fromPin = m.value(QStringLiteral("fromPin")).toString();
        const QString to = m.value(QStringLiteral("to")).toString(), toPin = m.value(QStringLiteral("toPin")).toString();
        const Node *a = node(from), *b = node(to);
        const auto *out = a ? a->type->output(fromPin) : nullptr;
        if (!out || !b || !b->type->input(toPin))
            continue;
        m_links << Link{from, fromPin, to, toPin, out->kind == nodes::PinKind::Flow};
    }
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT linksChanged();
}

QVariantMap ActionGraph::toMap() const
{
    QVariantList ns, ls;
    for (const Node &n : m_nodes) {
        QVariantMap m{{QStringLiteral("id"), n.uid}, {QStringLiteral("type"), n.type->id}, {QStringLiteral("x"), qRound(n.x)}, {QStringLiteral("y"), qRound(n.y)}};
        if (!n.values.isEmpty())
            m.insert(QStringLiteral("values"), n.values);
        ns << m;
    }
    for (const Link &l : m_links)
        ls << QVariantMap{{QStringLiteral("from"), l.from}, {QStringLiteral("fromPin"), l.fromPin}, {QStringLiteral("to"), l.to}, {QStringLiteral("toPin"), l.toPin}};
    return {{QStringLiteral("nodes"), ns}, {QStringLiteral("links"), ls}};
}

QString ActionGraph::addNode(const QString &type, qreal x, qreal y)
{
    const auto *t = nodes::find(type);
    if (!t || t->trigger)
        return {};
    Node n{nextUid(), t, x, y, {}};
    beginInsertRows({}, int(m_nodes.size()), int(m_nodes.size()));
    m_nodes << n;
    endInsertRows();
    Q_EMIT countChanged();
    Q_EMIT edited();
    return n.uid;
}

QString ActionGraph::duplicateNode(const QString &uid)
{
    const Node *src = node(uid);
    if (!src || src->type->trigger)
        return {};
    Node n{nextUid(), src->type, src->x + 32, src->y + 32, src->values};
    beginInsertRows({}, int(m_nodes.size()), int(m_nodes.size()));
    m_nodes << n;
    endInsertRows();
    Q_EMIT countChanged();
    Q_EMIT edited();
    return n.uid;
}

void ActionGraph::removeNode(const QString &uid)
{
    const int row = rowOf(uid);
    if (row < 0 || m_nodes[row].type->trigger)
        return;
    m_links.removeIf([&](const Link &l) { return l.from == uid || l.to == uid; });
    beginRemoveRows({}, row, row);
    m_nodes.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
    linksTouched();
}

void ActionGraph::moveNode(const QString &uid, qreal x, qreal y)
{
    const int row = rowOf(uid);
    if (row < 0 || (m_nodes[row].x == x && m_nodes[row].y == y))
        return;
    m_nodes[row].x = x;
    m_nodes[row].y = y;
    Q_EMIT dataChanged(index(row), index(row), {PosXRole, PosYRole});
    Q_EMIT edited();
}

void ActionGraph::setValue(const QString &uid, const QString &pin, const QVariant &value)
{
    const int row = rowOf(uid);
    if (row < 0 || !m_nodes[row].type->input(pin))
        return;
    Node &n = m_nodes[row];
    const auto *def = n.type->input(pin);
    QVariant v = value;
    if (def->kind == nodes::PinKind::Number)
        v = value.toDouble();
    else if (def->kind == nodes::PinKind::Bool)
        v = value.toBool();
    else
        v = value.toString();
    if (n.values.value(pin, def->defaultValue) == v)
        return;
    n.values.insert(pin, v);
    Q_EMIT dataChanged(index(row), index(row), {ValuesRole});
    Q_EMIT edited();
}

bool ActionGraph::dependsOn(const QString &uid, const QString &on) const
{
    // Walk data links upstream from `uid`.
    QList<QString> todo{uid};
    QSet<QString> seen;
    while (!todo.isEmpty()) {
        const QString cur = todo.takeLast();
        if (cur == on)
            return true;
        if (seen.contains(cur))
            continue;
        seen.insert(cur);
        for (const Link &l : m_links)
            if (!l.flow && l.to == cur)
                todo << l.from;
    }
    return false;
}

bool ActionGraph::canConnect(const QString &fromUid, const QString &fromPin, const QString &toUid, const QString &toPin) const
{
    if (fromUid == toUid)
        return false;
    const Node *a = node(fromUid), *b = node(toUid);
    if (!a || !b)
        return false;
    const auto *out = a->type->output(fromPin);
    const auto *in = b->type->input(toPin);
    if (!out || !in)
        return false;
    const bool flow = out->kind == nodes::PinKind::Flow;
    if (flow != (in->kind == nodes::PinKind::Flow))
        return false;
    // Data can't feed back into itself; flow loops are allowed (bounded at run time).
    return flow || !dependsOn(fromUid, toUid);
}

bool ActionGraph::connectPins(const QString &fromUid, const QString &fromPin, const QString &toUid, const QString &toPin)
{
    if (!canConnect(fromUid, fromPin, toUid, toPin))
        return false;
    const bool flow = node(fromUid)->type->output(fromPin)->kind == nodes::PinKind::Flow;
    // A flow output leads to one place; a data input reads from one place.
    m_links.removeIf([&](const Link &l) { return flow ? (l.from == fromUid && l.fromPin == fromPin) : (l.to == toUid && l.toPin == toPin); });
    m_links << Link{fromUid, fromPin, toUid, toPin, flow};
    linksTouched();
    return true;
}

void ActionGraph::disconnectPin(const QString &uid, const QString &pin, bool isInput)
{
    const auto removed =
        m_links.removeIf([&](const Link &l) { return isInput ? (l.to == uid && l.toPin == pin) : (l.from == uid && l.fromPin == pin); });
    if (removed)
        linksTouched();
}

QVariantMap ActionGraph::linkInto(const QString &uid, const QString &pin) const
{
    for (const Link &l : m_links)
        if (l.to == uid && l.toPin == pin)
            return {{QStringLiteral("from"), l.from}, {QStringLiteral("fromPin"), l.fromPin}, {QStringLiteral("flow"), l.flow}};
    return {};
}

QVariantMap ActionGraph::bounds() const
{
    if (m_nodes.isEmpty())
        return {};
    qreal x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (const Node &n : m_nodes) {
        const qreal h = 44 + 34 * std::max(n.type->inputs.size(), n.type->outputs.size());
        x0 = std::min(x0, n.x);
        y0 = std::min(y0, n.y);
        x1 = std::max(x1, n.x + n.type->width);
        y1 = std::max(y1, n.y + h);
    }
    return {{QStringLiteral("x"), x0}, {QStringLiteral("y"), y0}, {QStringLiteral("width"), x1 - x0}, {QStringLiteral("height"), y1 - y0}};
}

void ActionGraph::linksTouched()
{
    if (!m_nodes.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_nodes.size()) - 1), {LinkedInputsRole, LinkedOutputsRole});
    Q_EMIT linksChanged();
    Q_EMIT edited();
}
