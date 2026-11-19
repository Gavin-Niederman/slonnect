#pragma once

#include "smstypes.h"

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
        StatusRole, // pending | sent | delivered | read | failed | received
        SenderRole,
        SenderNameRole,
        SenderAvatarRole,
        AttachmentsRole,
        ShowDayRole,    // first message of its day (render a date chip above)
        GroupStartRole, // first of a run from the same sender
        GroupEndRole,   // last of a run from the same sender
        MultiRole,
        IdRole,
        ErrorRole,
        ReactionsRole,  // [{emoji, count, mine}]
        ReplyRole,      // {body, sender} of the message this answers, or empty
    };

    explicit MessagesModel(ContactStore *contacts, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset(const QString &threadId, bool multi);
    QString threadId() const { return m_threadId; }
    int realCount() const { return int(m_items.size()) - m_pendingCount; }

    // Adds a message, or updates it in place (status, reactions, late media).
    // Returns true if it was new.
    bool add(const SmsMessage &message);
    void remove(const QString &messageId);
    void addPending(const QString &body, const QStringList &attachmentPaths, const QString &tmpId);
    void markFailed(const QString &tmpId, const QString &error);
    void attachmentDownloaded(const QString &attachmentId, bool thumbnail, const QString &filePath);

    const SmsAttachment *findAttachment(const QString &attachmentId) const;
    const SmsMessage *findMessage(const QString &messageId) const;
    // Newest message from the other side, for read receipts.
    QString newestIncomingId() const;

    // Attachments we've learned the local file for (also across threads).
    QString localPathFor(const QString &attachmentId) const { return m_localFiles.value(attachmentId); }

Q_SIGNALS:
    void wantAttachment(const SmsAttachment &attachment, bool thumbnail);

private:
    struct Item {
        SmsMessage message;
        bool pending = false;
        QStringList pendingFiles;
    };

    QVariantList attachmentsFor(const Item &item) const;
    void requestMedia(const SmsMessage &message);
    void touchNeighbours(int row);
    bool sameGroup(int a, int b) const;
    QString senderOf(const Item &item) const;
    int rowOf(const QString &messageId) const;

    ContactStore *m_contacts;
    QString m_threadId;
    bool m_multi = false;
    QList<Item> m_items;
    QSet<QString> m_known;
    int m_pendingCount = 0;
    QHash<QString, QString> m_localFiles;
    QHash<QString, QString> m_thumbFiles;
    QSet<QString> m_thumbRequests;
    QHash<QString, QVariantMap> m_vcards;
};
