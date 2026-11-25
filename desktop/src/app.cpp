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
    , m_host(&m_devices)
    , m_plugins(&m_devices)
{
    m_demo = qEnvironmentVariableIsSet("SLONNECT_DEMO");
    if (m_demo) {
        m_sms.loadDemo();
        m_notifications.loadDemo();
    }
    // Demo mode must not put commands on a real phone.
    m_actions.setRegistering(!m_demo && !s_headless);
    m_link = std::make_unique<SlonnectLink>(&m_devices, &m_actions, m_demo);
    connect(&m_devices, &DeviceManager::currentChanged, this, &App::rebind);
    connect(&m_plugins, &PluginSettings::valuesChanged, this, [this](const QString &plugin) {
        if (plugin == QLatin1String("kdeconnect_share"))
            Q_EMIT m_share.incomingFolderChanged();
    });
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
        for (DevicePlugin *p : std::initializer_list<DevicePlugin *>{&m_share, &m_clipboard, &m_media, &m_remoteControl, &m_remoteKeyboard,
                                                                     &m_remoteCommands, &m_remoteVolume, &m_lockDevice, &m_virtualMonitor})
            p->setDevice(id, plugins);
        if (m_demo)
            return; // synthetic data only; never touch real messages, notifications or contacts
        m_notifications.setDevice(id, plugins);
        m_sms.setDevice(id, plugins);
        m_contacts.setDevice(id, plugins.contains(QStringLiteral("kdeconnect_contacts")));
        if (!m_pendingUrl.isEmpty() && m_sms.available()) {
            const QString url = std::exchange(m_pendingUrl, {});
            handleUrl(url);
        }
        if (!m_pendingDraft.isEmpty() && m_sms.available())
            Q_EMIT showMessages(); // the page picks the draft up from takeDraft()
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
    if (const QString body = QUrlQuery(u).queryItemValue(QStringLiteral("body"), QUrl::FullyDecoded); !body.isEmpty())
        m_pendingDraft = body;
    const QStringList recipients = u.path().split(QLatin1Char(','), Qt::SkipEmptyParts);
    m_sms.startConversation(recipients);
    Q_EMIT showMessages();
}

void App::handleArguments(const QStringList &args)
{
    QString page, plugin, device, message;
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args.at(i);
        auto next = [&] { return i + 1 < args.size() ? args.at(++i) : QString(); };
        if (a == QLatin1String("--device"))
            device = next();
        else if (a == QLatin1String("--plugin-config"))
            plugin = next();
        else if (a == QLatin1String("--message"))
            message = next();
        else if (a == QLatin1String("--page"))
            page = next();
        else if (a == QLatin1String("--activation-token"))
            qputenv("XDG_ACTIVATION_TOKEN", next().toUtf8()); // consumed by the window's requestActivate()
        else if (!a.startsWith(QLatin1Char('-')))
            handleUrl(a);
    }
    if (!device.isEmpty())
        m_devices.select(device);
    if (!plugin.isEmpty())
        page = plugin == QLatin1String("kdeconnect_runcommand") ? QStringLiteral("actions") : QStringLiteral("settings");
    Q_EMIT activateRequested(page, plugin);
    if (!message.isEmpty()) {
        // kdeconnect-sms --message: this text goes into the next conversation opened.
        m_pendingDraft = message;
        Q_EMIT showMessages();
    }
}

QString App::takeDraft()
{
    return std::exchange(m_pendingDraft, {});
}

QString App::formatSize(qint64 bytes) const
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}
