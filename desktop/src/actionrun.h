#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

namespace nodes {
struct NodeType;
}

// One execution of an action graph. Starts at the trigger node, follows flow links
// and resolves data inputs as each node runs. Deletes nothing itself; the owner
// (ActionStore) deletes it after finished().
//
// Flow is followed in strands. A run starts with one; "Run in parallel" forks child
// strands that execute at the same time (e.g. a desktop node and a phone node) and
// continues its own strand from "then" once all of them have reached their end.
class ActionRun : public QObject
{
    Q_OBJECT

public:
    ActionRun(const QString &actionId, const QVariantMap &graph, const QString &deviceId, QObject *parent = nullptr);

    void start();
    void cancel();

    QString actionId() const { return m_actionId; }
    QString deviceId() const { return m_deviceId; }
    QString deviceName() const { return m_deviceName; }
    QStringList logLines() const { return m_log; }
    bool isFinished() const { return m_finished; }

    void log(const QString &nodeUid, const QString &message, bool error = false);

    // Upper bound on node executions per run, so a flow loop can't spin forever.
    static constexpr int MaxSteps = 500;

Q_SIGNALS:
    // state: "running" | "done" | "failed"
    void nodeState(const QString &nodeUid, const QString &state);
    void logged(const QString &line);
    void finished(bool ok, const QString &error);

private:
    struct Node {
        QString uid;
        const nodes::NodeType *type = nullptr;
        QVariantMap values;
    };
    struct Link {
        QString from, fromPin, to, toPin;
    };
    struct Strand {
        // Flow pins still to follow, innermost last (so Sequence runs its branches in order).
        QList<QPair<QString, QString>> pending;
        bool busy = false;     // a node of this strand is executing
        int parent = -1;       // strand to resume when this one ends
        int children = 0;      // forked strands still going
        QString joinUid;       // node whose "then" pin continues after the children
        QString joinPin;
    };

    void begin();
    void step(int strand);
    void strandEnded(int strand);
    void execute(const Node &node, int strand);
    QVariant resolve(const Node &node, const QString &pin, int depth, QString *error);
    QVariantMap resolveInputs(const Node &node, int depth, QString *error);
    QString title(const QString &uid) const;
    void finish(bool ok, const QString &error = {});

    QString m_actionId;
    QString m_deviceId;
    QString m_deviceName;
    QHash<QString, Node> m_nodes;
    QList<Link> m_links;
    QHash<QString, QVariantMap> m_outputs;
    QHash<int, Strand> m_strands;
    int m_nextStrand = 0;
    // Executions that may still complete; a node's done()/fail() is ignored once its token is gone.
    QSet<quint64> m_live;
    quint64 m_nextToken = 0;
    QStringList m_log;
    int m_steps = 0;
    bool m_finished = false;
};
