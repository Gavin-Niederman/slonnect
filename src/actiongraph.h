#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace nodes {
struct NodeType;
}

// Editable copy of one action's graph for the node editor. Rows are nodes; links are
// a plain list (they change rarely, and wires bind to the node items' positions).
// Emits edited() after every change so the page can save.
class ActionGraph : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList links READ links NOTIFY linksChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        UidRole = Qt::UserRole + 1,
        TypeRole,
        TitleRole,
        CategoryRole,
        IconRole,
        DescriptionRole,
        PosXRole,
        PosYRole,
        WidthRole,
        InputsRole,
        OutputsRole,
        ValuesRole,
        LinkedInputsRole,
        LinkedOutputsRole,
        TriggerRole,
        PureRole,
        PluginRole,
    };

    explicit ActionGraph(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantList links() const;

    Q_INVOKABLE void load(const QVariantMap &graph);
    Q_INVOKABLE QVariantMap toMap() const;

    Q_INVOKABLE QString addNode(const QString &type, qreal x, qreal y);
    Q_INVOKABLE QString duplicateNode(const QString &uid);
    Q_INVOKABLE void removeNode(const QString &uid);
    Q_INVOKABLE void moveNode(const QString &uid, qreal x, qreal y);
    Q_INVOKABLE void setValue(const QString &uid, const QString &pin, const QVariant &value);

    Q_INVOKABLE bool canConnect(const QString &fromUid, const QString &fromPin, const QString &toUid, const QString &toPin) const;
    Q_INVOKABLE bool connectPins(const QString &fromUid, const QString &fromPin, const QString &toUid, const QString &toPin);
    Q_INVOKABLE void disconnectPin(const QString &uid, const QString &pin, bool isInput);
    // The link feeding an input, or an empty map.
    Q_INVOKABLE QVariantMap linkInto(const QString &uid, const QString &pin) const;
    // {x, y, width, height} around all nodes (heights estimated from pin rows).
    Q_INVOKABLE QVariantMap bounds() const;

Q_SIGNALS:
    void edited();
    void linksChanged();
    void countChanged();

private:
    struct Node {
        QString uid;
        const nodes::NodeType *type = nullptr;
        qreal x = 0, y = 0;
        QVariantMap values;
    };
    struct Link {
        QString from, fromPin, to, toPin;
        bool flow = false;
    };

    int rowOf(const QString &uid) const;
    const Node *node(const QString &uid) const;
    bool dependsOn(const QString &uid, const QString &on) const;
    void linksTouched();
    QString nextUid();

    QList<Node> m_nodes;
    QList<Link> m_links;
    int m_nextUid = 1;
};
