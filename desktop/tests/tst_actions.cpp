// Action engine tests: flow, branching, parallel strands and nodes run by another device.
// Only logic/data nodes and test-registered nodes are used, so nothing touches D-Bus,
// KDE Connect or the user's data.
#include "actionrun.h"
#include "nodetypes.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace {
struct Graph {
    QVariantList nodes, links;
    Graph &node(const QString &id, const QString &type, const QVariantMap &values = {})
    {
        nodes << QVariantMap{{u"id"_s, id}, {u"type"_s, type}, {u"values"_s, values}};
        return *this;
    }
    Graph &link(const QString &from, const QString &fromPin, const QString &to, const QString &toPin = u"in"_s)
    {
        links << QVariantMap{{u"from"_s, from}, {u"fromPin"_s, fromPin}, {u"to"_s, to}, {u"toPin"_s, toPin}};
        return *this;
    }
    QVariantMap map() const { return {{u"nodes"_s, nodes}, {u"links"_s, links}}; }
};

struct Result {
    bool ok = false;
    QString error;
    QStringList log;
    qint64 ms = 0;
};

Result runGraph(const Graph &g, int timeoutMs = 5000)
{
    ActionRun run(u"test"_s, g.map(), QString());
    QSignalSpy finished(&run, &ActionRun::finished);
    QElapsedTimer t;
    t.start();
    run.start();
    if (!finished.wait(timeoutMs))
        return {false, u"timeout"_s, run.logLines(), t.elapsed()};
    return {finished.at(0).at(0).toBool(), finished.at(0).at(1).toString(), run.logLines(), t.elapsed()};
}

// Index of the first log line containing `needle`, or -1.
int lineOf(const QStringList &log, const QString &needle)
{
    for (int i = 0; i < log.size(); ++i)
        if (log.at(i).contains(needle))
            return i;
    return -1;
}

// A node standing in for one the phone runs: waits `ms`, then reports back like SlonnectLink would.
void registerFakePhoneNode()
{
    nodes::NodeType t;
    t.id = u"mobile.test.echo"_s;
    t.title = u"Phone echo"_s;
    t.category = u"On the phone"_s;
    t.remote = true;
    t.inputs = {nodes::PinDef{u"in"_s, {}, nodes::PinKind::Flow}, nodes::PinDef{u"text"_s, u"Text"_s}, nodes::PinDef{u"ms"_s, u"ms"_s, nodes::PinKind::Number, 50}};
    t.outputs = {nodes::PinDef{u"out"_s, {}, nodes::PinKind::Flow}, nodes::PinDef{u"echo"_s, u"Echo"_s}};
    t.action = [](const nodes::Ctx &c) {
        QTimer::singleShot(int(c.num("ms")), c.run.data(), [c] {
            if (c.str("text") == u"fail"_s)
                return c.fail(u"phone said no"_s);
            c.next({{u"echo"_s, u"phone:"_s + c.str("text")}});
        });
    };
    nodes::registerExternal(t);
}
} // namespace

class ActionsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { registerFakePhoneNode(); }

    void runsInOrder()
    {
        Graph g;
        g.node(u"t"_s, nodes::TriggerType)
            .node(u"a"_s, u"logic.log"_s, {{u"message"_s, u"first"_s}})
            .node(u"b"_s, u"logic.log"_s, {{u"message"_s, u"second"_s}})
            .link(u"t"_s, u"out"_s, u"a"_s)
            .link(u"a"_s, u"out"_s, u"b"_s);
        const Result r = runGraph(g);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(lineOf(r.log, u"first"_s) >= 0);
        QVERIFY(lineOf(r.log, u"first"_s) < lineOf(r.log, u"second"_s));
    }

    void branches()
    {
        Graph g;
        g.node(u"t"_s, nodes::TriggerType)
            .node(u"if"_s, u"logic.if"_s, {{u"a"_s, 3}, {u"op"_s, u">"_s}, {u"b"_s, 2}})
            .node(u"yes"_s, u"logic.log"_s, {{u"message"_s, u"took-true"_s}})
            .node(u"no"_s, u"logic.log"_s, {{u"message"_s, u"took-false"_s}})
            .link(u"t"_s, u"out"_s, u"if"_s)
            .link(u"if"_s, u"true"_s, u"yes"_s)
            .link(u"if"_s, u"false"_s, u"no"_s);
        const Result r = runGraph(g);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(lineOf(r.log, u"took-true"_s) >= 0);
        QCOMPARE(lineOf(r.log, u"took-false"_s), -1);
    }

    // Two 300 ms branches finish in about 300 ms, not 600, and "then" waits for both.
    void parallelRunsAtOnceAndJoins()
    {
        Graph g;
        g.node(u"t"_s, nodes::TriggerType)
            .node(u"par"_s, u"logic.parallel"_s)
            .node(u"waitA"_s, u"logic.wait"_s, {{u"seconds"_s, 0.3}})
            .node(u"logA"_s, u"logic.log"_s, {{u"message"_s, u"A-done"_s}})
            .node(u"phone"_s, u"mobile.test.echo"_s, {{u"text"_s, u"hi"_s}, {u"ms"_s, 300}})
            .node(u"logB"_s, u"logic.log"_s)
            .node(u"after"_s, u"logic.log"_s, {{u"message"_s, u"joined"_s}})
            .link(u"t"_s, u"out"_s, u"par"_s)
            .link(u"par"_s, u"a"_s, u"waitA"_s)
            .link(u"waitA"_s, u"out"_s, u"logA"_s)
            .link(u"par"_s, u"b"_s, u"phone"_s)
            .link(u"phone"_s, u"out"_s, u"logB"_s)
            .link(u"phone"_s, u"echo"_s, u"logB"_s, u"message"_s)
            .link(u"par"_s, u"then"_s, u"after"_s);
        const Result r = runGraph(g);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY2(r.ms < 550, qPrintable(u"took %1 ms"_s.arg(r.ms)));
        const int a = lineOf(r.log, u"A-done"_s), b = lineOf(r.log, u"phone:hi"_s), joined = lineOf(r.log, u"joined"_s);
        QVERIFY(a >= 0 && b >= 0);
        QVERIFY(joined > a && joined > b);
    }

    // An unconnected branch counts as finished at once.
    void parallelWithUnusedBranches()
    {
        Graph g;
        g.node(u"t"_s, nodes::TriggerType)
            .node(u"par"_s, u"logic.parallel"_s)
            .node(u"after"_s, u"logic.log"_s, {{u"message"_s, u"joined"_s}})
            .link(u"t"_s, u"out"_s, u"par"_s)
            .link(u"par"_s, u"then"_s, u"after"_s);
        const Result r = runGraph(g);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(lineOf(r.log, u"joined"_s) >= 0);
    }

    // A failure on one branch stops the run; the other branch's later steps never run.
    void parallelFailureStopsRun()
    {
        Graph g;
        g.node(u"t"_s, nodes::TriggerType)
            .node(u"par"_s, u"logic.parallel"_s)
            .node(u"phone"_s, u"mobile.test.echo"_s, {{u"text"_s, u"fail"_s}, {u"ms"_s, 20}})
            .node(u"slow"_s, u"logic.wait"_s, {{u"seconds"_s, 0.3}})
            .node(u"late"_s, u"logic.log"_s, {{u"message"_s, u"too-late"_s}})
            .node(u"after"_s, u"logic.log"_s, {{u"message"_s, u"joined"_s}})
            .link(u"t"_s, u"out"_s, u"par"_s)
            .link(u"par"_s, u"a"_s, u"phone"_s)
            .link(u"par"_s, u"b"_s, u"slow"_s)
            .link(u"slow"_s, u"out"_s, u"late"_s)
            .link(u"par"_s, u"then"_s, u"after"_s);
        const Result r = runGraph(g);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(u"phone said no"_s));
        QTest::qWait(400);
        QCOMPARE(lineOf(r.log, u"too-late"_s), -1);
        QCOMPARE(lineOf(r.log, u"joined"_s), -1);
    }

    // Nested forks: the inner join resumes the inner branch, then the outer one joins.
    void nestedParallel()
    {
        Graph g;
        g.node(u"t"_s, nodes::TriggerType)
            .node(u"outer"_s, u"logic.parallel"_s)
            .node(u"inner"_s, u"logic.parallel"_s)
            .node(u"w1"_s, u"logic.wait"_s, {{u"seconds"_s, 0.1}})
            .node(u"w2"_s, u"logic.wait"_s, {{u"seconds"_s, 0.2}})
            .node(u"innerDone"_s, u"logic.log"_s, {{u"message"_s, u"inner-joined"_s}})
            .node(u"w3"_s, u"logic.wait"_s, {{u"seconds"_s, 0.05}})
            .node(u"outerDone"_s, u"logic.log"_s, {{u"message"_s, u"outer-joined"_s}})
            .link(u"t"_s, u"out"_s, u"outer"_s)
            .link(u"outer"_s, u"a"_s, u"inner"_s)
            .link(u"inner"_s, u"a"_s, u"w1"_s)
            .link(u"inner"_s, u"b"_s, u"w2"_s)
            .link(u"inner"_s, u"then"_s, u"innerDone"_s)
            .link(u"outer"_s, u"b"_s, u"w3"_s)
            .link(u"outer"_s, u"then"_s, u"outerDone"_s);
        const Result r = runGraph(g);
        QVERIFY2(r.ok, qPrintable(r.error));
        const int inner = lineOf(r.log, u"inner-joined"_s), outer = lineOf(r.log, u"outer-joined"_s);
        QVERIFY(inner >= 0);
        QVERIFY(outer > inner);
    }

    void externalTypesAreListed()
    {
        bool found = false;
        for (const QVariant &v : nodes::catalog()) {
            const QVariantMap t = v.toMap();
            if (t.value(u"id"_s) == u"mobile.test.echo"_s) {
                found = true;
                QVERIFY(t.value(u"remote"_s).toBool());
            }
        }
        QVERIFY(found);
        // Re-registering replaces the definition without invalidating the old pointer.
        const nodes::NodeType *before = nodes::find(u"mobile.test.echo"_s);
        nodes::NodeType copy = *before;
        copy.title = u"Phone echo v2"_s;
        nodes::registerExternal(copy);
        QCOMPARE(nodes::find(u"mobile.test.echo"_s)->title, u"Phone echo v2"_s);
        QCOMPARE(before->title, u"Phone echo"_s);
    }
};

QTEST_GUILESS_MAIN(ActionsTest)
#include "tst_actions.moc"
