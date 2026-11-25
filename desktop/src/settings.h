// KDE Connect settings that the official app and settings module expose:
// this computer's identity and transports, and each device's plugins.
#pragma once

#include <QAbstractListModel>
#include <QDBusMessage>
#include <QPointer>
#include <QStringList>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class Device;
class DeviceManager;

// This computer, as kdeconnectd announces it.
class HostSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.host")
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString id READ id NOTIFY changed)
    // [{id, name, enabled}] — LAN, Bluetooth, ...
    Q_PROPERTY(QVariantList backends READ backends NOTIFY changed)
    // Addresses kdeconnectd connects to directly, for networks where discovery
    // broadcasts don't arrive (VPNs, other subnets).
    Q_PROPERTY(QStringList customDevices READ customDevices NOTIFY changed)

public:
    explicit HostSettings(DeviceManager *devices, QObject *parent = nullptr);

    QString name() const { return m_name; }
    QString id() const { return m_id; }
    QVariantList backends() const { return m_backends; }
    QStringList customDevices() const { return m_customDevices; }

    Q_INVOKABLE void setName(const QString &name);
    Q_INVOKABLE void setBackendEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool addCustomDevice(const QString &address);
    Q_INVOKABLE void removeCustomDevice(const QString &address);

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void reload();
    void onNameChanged(const QString &name);
    void onBackendsChanged(const QStringList &providers);
    void onCustomDevicesChanged(const QStringList &devices);

private:
    void setCustomDevices(const QStringList &devices);

    QString m_name;
    QString m_id;
    QVariantList m_backends;
    QStringList m_customDevices;
};

// The selected device's plugins: which are enabled, plus their settings, which
// kdeconnectd keeps in ~/.config/kdeconnect/<device>/<plugin>/config.
class PluginSettings : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.plugins")
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY deviceChanged)

public:
    enum Roles { IdRole = Qt::UserRole + 1, EnabledRole, LoadedRole };

    explicit PluginSettings(DeviceManager *devices, QObject *parent = nullptr);

    QString deviceId() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void setEnabled(const QString &plugin, bool enabled);

    // Per-plugin settings, read and written the way kdeconnectd's
    // KdeConnectPluginConfig does, then announced so the daemon reloads them.
    Q_INVOKABLE QVariant value(const QString &plugin, const QString &key, const QVariant &fallback) const;
    Q_INVOKABLE void setValue(const QString &plugin, const QString &key, const QVariant &value);
    // Back to the daemon's default (an empty value would override it).
    Q_INVOKABLE void reset(const QString &plugin, const QString &key);

    static QString configPath(const QString &deviceId, const QString &plugin);

Q_SIGNALS:
    void deviceChanged();
    // Settings changed on disk (by us or by another KDE Connect UI).
    void valuesChanged(const QString &plugin);

private Q_SLOTS:
    void rebind();
    void reload();
    void onConfigChanged(const QDBusMessage &msg);

private:
    void announce(const QString &plugin);

    struct Entry {
        QString id;
        bool enabled = false;
        bool loaded = false;
    };

    DeviceManager *m_devices;
    QPointer<Device> m_device;
    QList<Entry> m_entries;
    QStringList m_watched; // plugins whose configChanged we listen to
};
