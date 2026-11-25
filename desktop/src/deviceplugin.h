// Base class for a feature backed by one KDE Connect device plugin.
//
// To add a feature: subclass, set the plugin id / object path / interface in the
// constructor, react in attached()/detached(), and expose it from App. The
// matching QML page is registered in qml/Features.qml.
#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class DevicePlugin : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY availableChanged)

public:
    DevicePlugin(const QString &pluginId, const QString &pathSuffix, const QString &iface, QObject *parent = nullptr);

    bool available() const { return m_available; }
    QString deviceId() const { return m_deviceId; }
    QString pluginId() const { return m_pluginId; }

    // Called by App whenever the selected device or its loaded plugins change.
    void setDevice(const QString &deviceId, const QStringList &loadedPlugins);

Q_SIGNALS:
    void availableChanged();

protected:
    virtual void attached() {}
    virtual void detached() {}

    QString path() const;
    QString iface() const { return m_iface; }

    // Call a method on this plugin's interface (async).
    void invoke(const QString &method, const QVariantList &args = {});

    // Subscribe to a D-Bus signal for the current device; undone on detach.
    // `slot` uses the SLOT() macro, e.g. SLOT(onFoo(QString)).
    void subscribe(const QString &signal, const char *slot, const QString &pathOverride = {}, const QString &ifaceOverride = {});

private:
    void detach();

    struct Subscription {
        QString path, iface, signal;
        QByteArray slot;
    };

    QString m_pluginId;
    QString m_pathSuffix;
    QString m_iface;
    QString m_deviceId;
    bool m_available = false;
    QList<Subscription> m_subscriptions;
};
