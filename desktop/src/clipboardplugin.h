#pragma once

#include "deviceplugin.h"

#include <QStringList>

// Clipboard sync (kdeconnect_clipboard) plus a local history of clips.
class ClipboardPlugin : public DevicePlugin
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.clipboard")
    Q_PROPERTY(QStringList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QString current READ current NOTIFY historyChanged)
    Q_PROPERTY(bool autoShare READ autoShare NOTIFY autoShareChanged)

public:
    explicit ClipboardPlugin(QObject *parent = nullptr);

    QStringList history() const { return m_history; }
    QString current() const { return m_history.value(0); }
    bool autoShare() const { return m_autoShare; }

    // Push what's on the desktop clipboard right now to the phone.
    Q_INVOKABLE void sendClipboard();
    // Put `text` on the desktop clipboard and push it to the phone.
    Q_INVOKABLE void sendText(const QString &text);
    Q_INVOKABLE void copy(const QString &text);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void clearHistory();

Q_SIGNALS:
    void historyChanged();
    void autoShareChanged();

protected:
    void attached() override;

private Q_SLOTS:
    void onAutoShareDisabledChanged(bool disabled);

private:
    void capture();
    void save() const;

    QStringList m_history;
    bool m_autoShare = true;
};
