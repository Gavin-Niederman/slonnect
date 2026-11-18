// Catalogue of node types for the action editor, and what each one does when run.
//
// An action is a small graph. Execution starts at the "Phone command" trigger and
// follows flow links (white wires) from node to node. Data inputs are pulled when a
// node runs: from the inline value typed into the node, or from a linked output.
// Pure nodes (no flow pins) are evaluated on demand; flow nodes expose the outputs
// of their last run.
//
// To add a node type: append a NodeType in nodetypes.cpp with its pins and either an
// `action` (async, has flow pins) or a `pure` function. The editor picks it up from
// the catalogue automatically.
#pragma once

#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <functional>

class ActionRun;

namespace nodes {

enum class PinKind { Flow, Text, Number, Bool, Choice };

struct PinDef {
    QString id;
    QString label;
    PinKind kind = PinKind::Text;
    QVariant defaultValue = {};
    QStringList options = {}; // Choice pins: stored values (also shown as labels)
    QString placeholder = {};
};

// What an action node's executor gets: resolved inputs plus completion callbacks.
// Call exactly one of next()/branch()/fail(), possibly later (async).
struct Ctx {
    QPointer<ActionRun> run;
    QString nodeUid;
    QVariantMap in;
    std::function<void(const QStringList &flows, const QVariantMap &outputs)> done;
    std::function<void(const QString &error)> fail;

    QString str(const char *id) const { return in.value(QLatin1String(id)).toString(); }
    double num(const char *id) const { return in.value(QLatin1String(id)).toDouble(); }
    bool flag(const char *id) const;
    // Continue along the default "out" flow pin.
    void next(const QVariantMap &outputs = {}) const { done({QStringLiteral("out")}, outputs); }
    void branch(const QString &flow, const QVariantMap &outputs = {}) const { done({flow}, outputs); }

    QString deviceId() const;
    QString devicePath(const QString &suffix = {}) const;
    void log(const QString &message) const;
};

using ActionFn = std::function<void(const Ctx &ctx)>;
using PureFn = std::function<QVariantMap(const QVariantMap &in)>;

struct NodeType {
    QString id;
    QString title;
    QString category;
    QString icon;
    QString description;
    QList<PinDef> inputs;
    QList<PinDef> outputs;
    ActionFn action; // flow nodes
    PureFn pure;     // data-only nodes
    int width = 240;
    bool trigger = false;
    // Needs a device plugin; the editor warns when the current device lacks it.
    QString plugin = {};

    bool isPure() const { return bool(pure); }
    const PinDef *input(const QString &id) const;
    const PinDef *output(const QString &id) const;
};

inline const QString TriggerType = QStringLiteral("trigger.phone");

const QList<NodeType> &all();
const NodeType *find(const QString &id);
QString kindName(PinKind kind);
QVariantList describePins(const QList<PinDef> &pins);
// The catalogue as plain maps for QML (one entry per node type).
QVariantList catalog();
QVariantMap describe(const NodeType &t);

} // namespace nodes
