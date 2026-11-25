#pragma once

#include "device.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

// All devices known to kdeconnectd plus the one the UI is focused on.
class DeviceManager : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.devices")
    Q_PROPERTY(Device *current READ current NOTIFY currentChanged)
    Q_PROPERTY(bool daemonRunning READ daemonRunning NOTIFY daemonRunningChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // A device waiting for us to accept or reject its pairing request (first one if several).
    Q_PROPERTY(Device *pairingRequest READ pairingRequest NOTIFY pairingRequestChanged)

public:
    enum Roles { DeviceRole = Qt::UserRole + 1, NameRole, ReachableRole, PairedRole, IdRole };

    explicit DeviceManager(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Device *current() const { return m_current; }
    bool daemonRunning() const { return m_daemonRunning; }
    Device *pairingRequest() const { return m_pairingRequest; }
    Device *find(const QString &id) const;

    Q_INVOKABLE void select(const QString &id);
    Q_INVOKABLE void refreshDiscovery();

Q_SIGNALS:
    void currentChanged();
    void daemonRunningChanged();
    void countChanged();
    void pairingRequestChanged();
    // A device's pairing attempt failed (rejected, timed out, ...).
    void pairingFailed(const QString &deviceName, const QString &error);

private Q_SLOTS:
    void reload();

private:
    void pickCurrent();
    void setDaemonRunning(bool running);
    void updatePairingRequest();

    QList<Device *> m_devices;
    QPointer<Device> m_current;
    QPointer<Device> m_pairingRequest;
    QString m_preferredId;
    bool m_daemonRunning = false;
};
