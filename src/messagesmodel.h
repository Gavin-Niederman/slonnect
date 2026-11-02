#pragma once

#include "messagetypes.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QtQml/qqmlregistration.h>

class ContactStore;

// Messages of the open thread, newest first (index 0) so a bottom-to-top
// ListView can append history at the end cheaply.
class MessagesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.sms.messages")

public:
    enum Roles {
        BodyRole = Qt::UserRole + 1,
        DateRole,
        OutgoingRole,
        StatusRole, // "sent" | "pending" | "failed" | "received"
        SenderRole,
        SenderNameRole,
        SenderAvatarRole,
        AttachmentsRole,
        ShowDayRole,    // first message of its day (render a date chip above)
        GroupStartRole, // first of a run from the same sender
        GroupEndRole,   // last of a run from the same sender
        MultiRole,
    };

    explicit MessagesModel(ContactStore *contacts, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset(qint64 threadId, bool multi);
    qint64 threadId() const { return m_threadId; }
    int realCount() const { return int(m_items.size()) - m_pendingCount; }

    // Returns true if the message was new.
    bool add(const ConversationMessage &message);
    void addPending(const QString &body, const QStringList &attachmentPaths);
    void attachmentDownloaded(const QString &uniqueIdentifier, const QString &filePath);

    // Attachments we've learned the local file for (also across threads).
    QString localPathFor(const QString &uniqueIdentifier) const { return m_localFiles.value(uniqueIdentifier); }

Q_SIGNALS:
    void wantAttachment(qint64 partId, const QString &uniqueIdentifier);

private:
    struct Item {
        ConversationMessage message;
        bool pending = false;
        QStringList pendingFiles;
    };

    QVariantList attachmentsFor(const Item &item) const;
    void touchNeighbours(int row);
    bool sameGroup(int a, int b) const;
    QString senderOf(const Item &item) const;

    ContactStore *m_contacts;
    qint64 m_threadId = -1;
    bool m_multi = false;
    QList<Item> m_items;
    QSet<qint32> m_known;
    int m_pendingCount = 0;
    QHash<QString, QString> m_localFiles;
    QHash<QString, QVariantMap> m_vcards;
};
