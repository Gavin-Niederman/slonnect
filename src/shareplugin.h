#pragma once

#include "deviceplugin.h"

#include <QVariantList>

// File / link / text sharing (kdeconnect_share).
class SharePlugin : public DevicePlugin
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.share")
    Q_PROPERTY(QVariantList received READ received NOTIFY receivedChanged)
    Q_PROPERTY(QString incomingFolder READ incomingFolder NOTIFY availableChanged)

public:
    explicit SharePlugin(QObject *parent = nullptr);

    QVariantList received() const { return m_received; }
    QString incomingFolder() const;

    // Accepts file:// URLs, local paths or web links.
    Q_INVOKABLE int shareUrls(const QVariantList &urls);
    Q_INVOKABLE void shareText(const QString &text);
    Q_INVOKABLE void openIncomingFolder() const;
    Q_INVOKABLE void openReceived(const QString &url) const;
    Q_INVOKABLE void revealReceived(const QString &url) const;
    Q_INVOKABLE void clearReceived();

Q_SIGNALS:
    void receivedChanged();
    void fileReceived(const QString &name);

protected:
    void attached() override;

private Q_SLOTS:
    void onShareReceived(const QString &url);

private:
    void load();
    void save() const;

    QVariantList m_received;
};
