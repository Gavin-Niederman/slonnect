#include "app.h"

#include <QLocale>
#include <QQmlEngine>
#include <QUrl>
#include <QUrlQuery>

App *App::instance()
{
    static App *app = new App;
    return app;
}

static bool s_headless = false;

void App::setHeadless(bool headless)
{
    s_headless = headless;
}

App *App::create(QQmlEngine *, QJSEngine *)
{
    QQmlEngine::setObjectOwnership(instance(), QQmlEngine::CppOwnership);
    return instance();
}

App::App(QObject *parent)
    : QObject(parent)
    , m_sms(&m_contacts)
    , m_actions(&m_devices)
{
    m_demo = qEnvironmentVariableIsSet("SLONNECT_DEMO");
    if (m_demo)
        m_sms.loadDemo();
    // Demo mode must not put commands on a real phone.
    m_actions.setRegistering(!m_demo && !s_headless);
    connect(&m_devices, &DeviceManager::currentChanged, this, &App::rebind);
    rebind();
}

void App::rebind()
{
    if (m_boundDevice)
        disconnect(m_boundDevice, nullptr, this, nullptr);
    m_boundDevice = m_devices.current();

    auto apply = [this] {
        Device *d = m_devices.current();
        const QString id = d && d->paired() ? d->id() : QString();
        const QStringList plugins = d && d->paired() && d->reachable() ? d->loadedPlugins() : QStringList();
        for (DevicePlugin *p : std::initializer_list<DevicePlugin *>{&m_share, &m_clipboard, &m_media})
            p->setDevice(id, plugins);
        if (m_demo)
            return; // synthetic conversations only; never touch real messages or contacts
        m_sms.setDevice(id, plugins);
        m_contacts.setDevice(id, plugins.contains(QStringLiteral("kdeconnect_contacts")));
        if (!m_pendingUrl.isEmpty() && m_sms.available()) {
            const QString url = std::exchange(m_pendingUrl, {});
            handleUrl(url);
        }
    };
    if (m_boundDevice) {
        connect(m_boundDevice, &Device::pluginsChanged, this, apply);
        connect(m_boundDevice, &Device::changed, this, apply);
    }
    apply();
    Q_EMIT deviceChanged();
}

QString App::version() const
{
    return QStringLiteral(SLONNECT_VERSION);
}

void App::handleUrl(const QString &url)
{
    const QUrl u(url);
    if (u.scheme() != QLatin1String("sms") && u.scheme() != QLatin1String("smsto") && u.scheme() != QLatin1String("tel"))
        return;
    if (!m_sms.available()) {
        m_pendingUrl = url;
        return;
    }
    const QStringList recipients = u.path().split(QLatin1Char(','), Qt::SkipEmptyParts);
    m_sms.startConversation(recipients);
    Q_EMIT showMessages();
}

QString App::formatSize(qint64 bytes) const
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}
