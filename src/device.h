#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

// One device known to kdeconnectd, with its core state and battery.
class Device : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by DeviceManager")
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString type READ type NOTIFY changed)
    Q_PROPERTY(bool reachable READ reachable NOTIFY changed)
    Q_PROPERTY(bool paired READ paired NOTIFY changed)
    Q_PROPERTY(bool pairRequested READ pairRequested NOTIFY changed)
    Q_PROPERTY(bool pairRequestedByPeer READ pairRequestedByPeer NOTIFY changed)
    Q_PROPERTY(QString verificationKey READ verificationKey NOTIFY changed)
    Q_PROPERTY(QStringList addresses READ addresses NOTIFY changed)
    Q_PROPERTY(QStringList providers READ providers NOTIFY changed)
    Q_PROPERTY(QStringList supportedPlugins READ supportedPlugins NOTIFY changed)
    Q_PROPERTY(QString encryptionInfo READ encryptionInfo NOTIFY encryptionInfoChanged)
    Q_PROPERTY(QStringList loadedPlugins READ loadedPlugins NOTIFY pluginsChanged)
    Q_PROPERTY(bool hasBattery READ hasBattery NOTIFY batteryChanged)
    Q_PROPERTY(int charge READ charge NOTIFY batteryChanged)
    Q_PROPERTY(bool charging READ charging NOTIFY batteryChanged)
    Q_PROPERTY(bool browsing READ browsing NOTIFY browsingChanged)
    // Mobile network report (kdeconnect_connectivity_report): strength 0-4, -1 = no signal.
    Q_PROPERTY(bool hasConnectivity READ hasConnectivity NOTIFY connectivityChanged)
    Q_PROPERTY(int signalStrength READ signalStrength NOTIFY connectivityChanged)
    Q_PROPERTY(QString networkType READ networkType NOTIFY connectivityChanged)

public:
    explicit Device(const QString &id, QObject *parent = nullptr);
    ~Device() override;

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QString type() const { return m_type; }
    bool reachable() const { return m_reachable; }
    bool paired() const { return m_paired; }
    bool pairRequested() const { return m_pairRequested; }
    bool pairRequestedByPeer() const { return m_pairRequestedByPeer; }
    QString verificationKey() const { return m_verificationKey; }
    QStringList addresses() const { return m_addresses; }
    QStringList providers() const { return m_providers; }
    QStringList supportedPlugins() const { return m_supportedPlugins; }
    QString encryptionInfo() const { return m_encryptionInfo; }
    QStringList loadedPlugins() const { return m_loadedPlugins; }
    bool hasBattery() const { return m_hasBattery; }
    int charge() const { return m_charge; }
    bool charging() const { return m_charging; }
    bool browsing() const { return m_browsing; }
    bool hasConnectivity() const { return m_connectivityConnected; }
    int signalStrength() const { return m_signalStrength; }
    QString networkType() const { return m_networkType; }

    Q_INVOKABLE bool hasPlugin(const QString &plugin) const { return m_loadedPlugins.contains(plugin); }

    // Small device actions that don't warrant their own page.
    Q_INVOKABLE void requestPairing();
    Q_INVOKABLE void acceptPairing();
    Q_INVOKABLE void cancelPairing();
    Q_INVOKABLE void unpair();
    Q_INVOKABLE void ping();
    Q_INVOKABLE void ring();
    Q_INVOKABLE void browseFiles();
    // Certificate fingerprints and protocol version; fills `encryptionInfo`.
    Q_INVOKABLE void loadEncryptionInfo();

Q_SIGNALS:
    void changed();
    void pluginsChanged();
    void batteryChanged();
    void browsingChanged();
    void browseFailed(const QString &error);
    void pairingFailed(const QString &error);
    void connectivityChanged();
    void encryptionInfoChanged();

private Q_SLOTS:
    void refresh();
    void refreshPlugins();
    void refreshBattery();
    void onBatteryRefreshed(bool charging, int charge);
    void onConnectivityRefreshed(const QString &type, int strength);
    void onPairingFailed(const QString &error);

private:
    void connectBattery(bool on);
    void connectConnectivity(bool on);
    QString path() const;

    QString m_id;
    QString m_name;
    QString m_type;
    QString m_verificationKey;
    QStringList m_addresses;
    QStringList m_providers;
    QStringList m_supportedPlugins;
    QString m_encryptionInfo;
    bool m_reachable = false;
    bool m_paired = false;
    bool m_pairRequested = false;
    bool m_pairRequestedByPeer = false;
    QStringList m_loadedPlugins;
    bool m_hasBattery = false;
    bool m_batteryConnected = false;
    int m_charge = -1;
    bool m_charging = false;
    bool m_browsing = false;
    bool m_connectivityConnected = false;
    int m_signalStrength = -1;
    QString m_networkType;
};
