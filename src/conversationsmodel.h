#pragma once

#include "messagetypes.h"

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QtQml/qqmlregistration.h>

class ContactStore;

// One row per thread, holding its newest message; kept sorted newest first.
class ConversationsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.sms.conversations")

public:
    enum Roles {
        ThreadIdRole = Qt::UserRole + 1,
        BodyRole,
        DateRole,
        ReadRole,
        OutgoingRole,
        AddressesRole,
        MultiRole,
        TitleRole,
        AvatarRole,
        AttachmentRole,
        SearchRole,
    };

    explicit ConversationsModel(ContactStore *contacts, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void upsert(const ConversationMessage &message);
    void remove(qint64 threadId);
    void clear();

    const ConversationMessage *find(qint64 threadId) const;
    // Thread whose participants match `addresses` exactly (normalised), or -1.
    qint64 threadFor(const QStringList &addresses) const;

    QString titleFor(const QStringList &addresses) const;

private:
    int indexOf(qint64 threadId) const;
    int insertPosition(qint64 date) const;

    ContactStore *m_contacts;
    QList<ConversationMessage> m_rows;
};

class ConversationsFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.sms.conversations")
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    QString search() const { return m_search; }
    void setSearch(const QString &text);

Q_SIGNALS:
    void searchChanged();

private:
    QString m_search;
};
