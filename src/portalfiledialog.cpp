#include "portalfiledialog.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QGuiApplication>
#include <QRandomGenerator>
#include <QWindow>

static const QString PortalService = QStringLiteral("org.freedesktop.portal.Desktop");
static const QString PortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
static const QString RequestIface = QStringLiteral("org.freedesktop.portal.Request");

static QString parentWindowId()
{
    // X11 windows can be identified directly; on Wayland an empty id is allowed
    // (the chooser just isn't attached to our window).
    if (QGuiApplication::platformName() == QLatin1String("xcb")) {
        if (QWindow *w = QGuiApplication::focusWindow())
            return QStringLiteral("x11:") + QString::number(w->winId(), 16);
    }
    return QString();
}

void PortalFileDialog::open()
{
    if (busy())
        return;
    auto bus = QDBusConnection::sessionBus();
    const QString token = QStringLiteral("slonnect%1").arg(QRandomGenerator::global()->generate());
    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    m_handle = PortalPath + QStringLiteral("/request/") + sender + QLatin1Char('/') + token;
    // Subscribe before calling so the Response can't race us.
    bus.connect(PortalService, m_handle, RequestIface, QStringLiteral("Response"), this, SLOT(onResponse(uint, QVariantMap)));
    Q_EMIT busyChanged();

    QVariantMap options{{QStringLiteral("handle_token"), token}, {QStringLiteral("multiple"), m_multiple}, {QStringLiteral("modal"), true}};
    if (!m_acceptLabel.isEmpty())
        options.insert(QStringLiteral("accept_label"), m_acceptLabel);

    auto msg = QDBusMessage::createMethodCall(PortalService, PortalPath, QStringLiteral("org.freedesktop.portal.FileChooser"), QStringLiteral("OpenFile"));
    msg.setArguments({parentWindowId(), m_title, options});
    auto *w = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qWarning() << "File chooser portal unavailable:" << reply.errorMessage();
            finish();
            Q_EMIT unavailable();
            return;
        }
        // Older portals may ignore handle_token and return a different handle.
        const QString handle = reply.arguments().value(0).value<QDBusObjectPath>().path();
        if (!handle.isEmpty() && handle != m_handle) {
            auto bus = QDBusConnection::sessionBus();
            bus.disconnect(PortalService, m_handle, RequestIface, QStringLiteral("Response"), this, SLOT(onResponse(uint, QVariantMap)));
            m_handle = handle;
            bus.connect(PortalService, m_handle, RequestIface, QStringLiteral("Response"), this, SLOT(onResponse(uint, QVariantMap)));
        }
    });
}

void PortalFileDialog::onResponse(uint response, const QVariantMap &results)
{
    finish();
    const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
    if (response == 0 && !uris.isEmpty())
        Q_EMIT accepted(uris);
    else
        Q_EMIT rejected();
}

void PortalFileDialog::finish()
{
    if (m_handle.isEmpty())
        return;
    QDBusConnection::sessionBus().disconnect(PortalService, m_handle, RequestIface, QStringLiteral("Response"), this, SLOT(onResponse(uint, QVariantMap)));
    m_handle.clear();
    Q_EMIT busyChanged();
}
