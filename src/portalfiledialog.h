#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

// File picker backed by the XDG desktop portal (org.freedesktop.portal.FileChooser),
// so the desktop's own chooser is used regardless of Qt platform theme.
class PortalFileDialog : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString title MEMBER m_title NOTIFY changed)
    Q_PROPERTY(QString acceptLabel MEMBER m_acceptLabel NOTIFY changed)
    Q_PROPERTY(bool multiple MEMBER m_multiple NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    using QObject::QObject;
    bool busy() const { return !m_handle.isEmpty(); }

    Q_INVOKABLE void open();

Q_SIGNALS:
    void accepted(const QStringList &urls);
    void rejected();
    // No portal on this session; the caller should fall back to another dialog.
    void unavailable();
    void changed();
    void busyChanged();

private Q_SLOTS:
    void onResponse(uint response, const QVariantMap &results);

private:
    void finish();

    QString m_title;
    QString m_acceptLabel;
    bool m_multiple = true;
    QString m_handle;
};
