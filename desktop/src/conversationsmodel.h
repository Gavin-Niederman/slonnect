#pragma once

#include "smstypes.h"

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
        RcsRole,
    };

    explicit ConversationsModel(ContactStore *contacts, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Newer data for a thread replaces the row; an older head is ignored.
    void upsert(const SmsThread &thread);
    void remove(const QString &threadId);
    void clear();

    const SmsThread *find(const QString &threadId) const;
    // Thread whose participants match `addresses` exactly (normalised), or "".
    QString threadFor(const QStringList &addresses) const;

    QString titleFor(const QStringList &addresses) const;
    // The backend's name for the thread when it has one, else ours from contacts.
    QString titleFor(const SmsThread &thread) const;

private:
    int indexOf(const QString &threadId) const;
    int insertPosition(qint64 date) const;

    ContactStore *m_contacts;
    QList<SmsThread> m_rows;
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
