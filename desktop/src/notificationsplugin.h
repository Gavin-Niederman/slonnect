#pragma once

#include "deviceplugin.h"

#include <QAbstractListModel>
#include <QDateTime>

// List model of the phone's notifications, newest first.
class NotificationsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.notifications.model")

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        AppNameRole,
        TitleRole,
        TextRole,
        IconRole,
        DismissableRole,
        RepliableRole,
        SilentRole,
        ArrivedRole,
    };

    struct Entry {
        QString id;
        QString appName, title, text, iconPath;
        bool dismissable = false;
        bool repliable = false;
        bool silent = false;
        QDateTime arrived; // invalid for notifications that predate this session
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void upsert(const Entry &e);
    void remove(const QString &id);
    void clear();
    const QList<Entry> &entries() const { return m_entries; }

private:
    QList<Entry> m_entries;
};

// The phone's notifications (kdeconnect_notifications): list, dismiss, reply.
// kdeconnectd already pops each one up as a desktop notification; this is the
// persistent list that the Plasma applet offers.
class NotificationsPlugin : public DevicePlugin
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.notifications")
    Q_PROPERTY(NotificationsModel *model READ model CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool anyDismissable READ anyDismissable NOTIFY countChanged)

public:
    explicit NotificationsPlugin(QObject *parent = nullptr);

    NotificationsModel *model() { return &m_model; }
    int count() const { return m_model.rowCount(); }
    bool anyDismissable() const;
    void loadDemo();

    Q_INVOKABLE void dismiss(const QString &id);
    Q_INVOKABLE void dismissAll();
    Q_INVOKABLE void reply(const QString &id, const QString &message);

Q_SIGNALS:
    void countChanged();

protected:
    void attached() override;
    void detached() override;

private Q_SLOTS:
    void onPosted(const QString &id);
    void onRemoved(const QString &id);
    void onAllRemoved();

private:
    void fetch(const QString &id, bool fresh);
    QString notificationPath(const QString &id) const;

    NotificationsModel m_model;
    bool m_demo = false;
};
