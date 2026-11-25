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

#include <QObject>
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
    // Editor suggestions: "dbus.service", "dbus.path", "dbus.interface", "dbus.method",
    // "bluetooth.device", "mpris.player" (see Completer).
    QString completion = {};
};

// What an action node's executor gets: resolved inputs plus completion callbacks.
// Call exactly one of next()/branch()/fail(), possibly later (async).
struct Ctx {
    QPointer<ActionRun> run;
    QString nodeUid;
    QVariantMap in;
    std::function<void(const QStringList &flows, const QVariantMap &outputs)> done;
    std::function<void(const QString &error)> fail;
    // Run `branches` (flow pins of this node) at the same time, then continue from `join`
    // once every branch has reached its end. Used by "Run in parallel".
    std::function<void(const QStringList &branches, const QString &join, const QVariantMap &outputs)> fork;

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
    // Executed by the phone (registered from its catalogue, see remotenodes.h).
    bool remote = false;
    // Non-empty when the phone said it can't run this node yet (e.g. Shizuku not running).
    QString unavailableReason = {};

    bool isPure() const { return bool(pure); }
    const PinDef *input(const QString &id) const;
    const PinDef *output(const QString &id) const;
};

inline const QString TriggerType = QStringLiteral("trigger.phone");

// Built-in node types.
const QList<NodeType> &all();
// Built-in or registered by another device (see registerExternal).
const NodeType *find(const QString &id);

// Node types another device executes for us: the phone's catalogue (remotenodes.h).
// Replaces an earlier type with the same id. Pointers from find() stay valid for the
// life of the process, so a run that started with the old definition is unaffected.
void registerExternal(const NodeType &type);
// Registered external types, latest definition of each.
QList<const NodeType *> externals();

// Notifies the editor when the catalogue changes.
class Registry : public QObject
{
    Q_OBJECT
public:
    static Registry *instance();
Q_SIGNALS:
    void changed();
};
QString kindName(PinKind kind);
QVariantList describePins(const QList<PinDef> &pins);
// The catalogue as plain maps for QML (one entry per node type).
QVariantList catalog();
QVariantMap describe(const NodeType &t);

} // namespace nodes
