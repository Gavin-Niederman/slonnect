#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

struct Contact {
    QString uid;
    QString name;
    QStringList numbers;
    QStringList emails;
    QString photoKey; // ImageStore key, empty if none
    QString filePath; // the source .vcf
    QByteArray raw;   // original vCard text
};

// Phone contacts synced by the KDE Connect contacts plugin as vCards under
// ~/.local/share/kpeoplevcard/kdeconnect-<device>. Parsed off the GUI thread.
class ContactStore : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.contacts")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    // True when the last sync request got no answer from the phone at all.
    Q_PROPERTY(bool phoneSilent READ phoneSilent NOTIFY syncingChanged)
    Q_PROPERTY(QSortFilterProxyModel *filtered READ filtered CONSTANT)

public:
    enum Roles { UidRole = Qt::UserRole + 1, NameRole, NumbersRole, PrimaryNumberRole, AvatarRole, SearchRole };

    explicit ContactStore(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool syncing() const { return m_syncing; }
    bool phoneSilent() const { return m_phoneSilent; }
    QSortFilterProxyModel *filtered() { return &m_filtered; }

    void setDevice(const QString &deviceId, bool contactsPluginLoaded);

    // Display name for a phone number / email / RCS business address.
    Q_INVOKABLE QString nameFor(const QString &address) const;
    Q_INVOKABLE QString avatarFor(const QString &address) const;
    Q_INVOKABLE bool isKnown(const QString &address) const;
    Q_INVOKABLE void sync();
    Q_INVOKABLE void setFilter(const QString &text);

    // Writes the contact's vCard to a temp file named after them; returns its path.
    Q_INVOKABLE QString exportVCard(const QString &uid) const;

    static QString prettyAddress(const QString &address);
    static QString normalize(const QString &address);
    static Contact parseVCard(const QByteArray &data, const QString &photoKeyHint = {});

Q_SIGNALS:
    void countChanged();
    void syncingChanged();
    void contactsChanged();

private Q_SLOTS:
    void onLocalCacheSynchronized();

private:
    void reload();

    QString m_deviceId;
    QString m_dir;
    bool m_pluginLoaded = false;
    bool m_syncing = false;
    bool m_phoneSilent = false;
    QList<Contact> m_contacts;
    QHash<QString, int> m_byNumber;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
    QSortFilterProxyModel m_filtered;
    quint64 m_generation = 0;
};
