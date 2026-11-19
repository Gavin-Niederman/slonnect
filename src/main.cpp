#include "actionservice.h"
#include "app.h"
#include "imagestore.h"
#include "messagetypes.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

static void setIdentity()
{
    // Demo mode keeps its settings apart from real ones.
    QGuiApplication::setOrganizationName(qEnvironmentVariableIsSet("SLONNECT_DEMO") ? QStringLiteral("slonnect-demo") : QStringLiteral("slonnect"));
    QGuiApplication::setApplicationName(QStringLiteral("slonnect"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Slonnect"));
    QGuiApplication::setDesktopFileName(QStringLiteral("dev.slonnect.Slonnect"));
}

// `slonnect --run-action <id> [--device <id>]`: what KDE Connect runs when the phone taps
// one of our commands. Hands the action to the open Slonnect, or runs it here without
// a window and exits when it's done.
static int runAction(int argc, char *argv[], const QString &actionId, const QString &deviceId)
{
    // kdeconnectd normally has the session's display; without one, run offscreen.
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    setIdentity();
    registerMessageTypes();
    new ImageStore; // contacts parsed in the background store their photos here

    if (ActionService::forward(actionId, deviceId))
        return 0;

    App::setHeadless(true);
    auto *actions = App::instance()->actions();
    ActionService service(actions);
    // Own the bus name while running so further taps queue up here; if another
    // instance grabbed it in the meantime, give the action to that one.
    if (!service.registerOnBus(false) && ActionService::forward(actionId, deviceId))
        return 0;

    if (actions->indexOf(actionId) < 0) {
        qWarning("slonnect: no action with id %s", qPrintable(actionId));
        return 2;
    }
    int status = 0;
    QObject::connect(actions, &ActionStore::runFinished, &app, [&status](const QString &, bool ok) { status = ok ? 0 : 1; });
    QObject::connect(actions, &ActionStore::idle, &app, &QCoreApplication::quit, Qt::QueuedConnection);
    QTimer::singleShot(std::chrono::minutes(30), &app, [] { QCoreApplication::exit(3); });
    actions->run(actionId, deviceId);
    return app.exec() == 3 ? 3 : status;
}

int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    for (int i = 1; i + 1 < argc; ++i) {
        if (qstrcmp(argv[i], "--run-action") != 0)
            continue;
        QString device;
        for (int j = 1; j + 1 < argc; ++j)
            if (qstrcmp(argv[j], "--device") == 0)
                device = QString::fromLocal8Bit(argv[j + 1]);
        return runAction(argc, argv, QString::fromLocal8Bit(argv[i + 1]), device);
    }

    QGuiApplication app(argc, argv);
    setIdentity();

    // One window: a second launch (sms: link, kdeconnect-app/-sms style flags) goes
    // to the running instance.
    QStringList forwarded = app.arguments().mid(1);
    // On Wayland the launcher's activation token lets the running window take focus.
    if (const QString token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN"); !token.isEmpty())
        forwarded << QStringLiteral("--activation-token") << token;
    if (!qEnvironmentVariableIsSet("SLONNECT_SCREENSHOT") && !qEnvironmentVariableIsSet("SLONNECT_SELFTEST") && AppService::forward(forwarded))
        return 0;

    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("kdeconnect")));

    QQuickStyle::setStyle(QStringLiteral("Material"));
    registerMessageTypes();

    // Created before App so contact photos parsed at startup have somewhere to go;
    // the engine takes ownership.
    auto *images = new ImageStore;

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("slonnect"), images);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Slonnect", "Main");

    // Dev aid: SLONNECT_SCREENSHOT=out.png [SLONNECT_PAGE=media] renders, saves a frame and quits.
    if (const QString shot = qEnvironmentVariable("SLONNECT_SCREENSHOT"); !shot.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        if (window && qEnvironmentVariableIsSet("SLONNECT_PAGE"))
            QMetaObject::invokeMethod(window, "go", Q_ARG(QVariant, qEnvironmentVariable("SLONNECT_PAGE")));
        if (const QString thread = qEnvironmentVariable("SLONNECT_THREAD"); !thread.isEmpty())
            QTimer::singleShot(1500, &app, [thread] { App::instance()->sms()->openThread(thread.toLongLong()); });
        QTimer::singleShot(qEnvironmentVariableIntegerValue("SLONNECT_SCREENSHOT_DELAY").value_or(3000), &app, [window, shot] {
            if (window)
                window->grabWindow().save(shot);
            QCoreApplication::quit();
        });
    }

    // Dev aid: SLONNECT_SELFTEST=1 opens the newest real thread, pages once, and prints
    // only counts (never message content) before quitting.
    if (qEnvironmentVariableIsSet("SLONNECT_SELFTEST")) {
        auto *sms = App::instance()->sms();
        auto report = [sms](const char *stage) {
            int withAttachments = 0;
            auto *m = sms->messages();
            for (int i = 0; i < m->rowCount(); ++i)
                withAttachments += !m->data(m->index(i), MessagesModel::AttachmentsRole).toList().isEmpty();
            qInfo("selftest %s: conversations=%d contacts=%d messages=%d withAttachments=%d loading=%d reachedStart=%d", stage,
                  sms->conversations()->rowCount(), App::instance()->contacts()->rowCount(), m->rowCount(), withAttachments,
                  sms->loadingMessages(), sms->reachedStart());
        };
        QTimer::singleShot(4000, &app, [sms, report] {
            auto *c = sms->conversations();
            if (c->rowCount() == 0) {
                report("no-conversations");
                return QCoreApplication::exit(2);
            }
            sms->openThread(c->data(c->index(0, 0), ConversationsModel::ThreadIdRole).toLongLong());
        });
        QTimer::singleShot(12000, &app, [sms, report] {
            report("first-page");
            sms->loadOlder();
        });
        QTimer::singleShot(22000, &app, [report] {
            report("after-loadOlder");
            QCoreApplication::quit();
        });
    }

    // Receive phone-triggered actions and later launches. Queued, so we take over
    // from a headless run.
    AppService appService;
    appService.registerObject();
    ActionService actionService(App::instance()->actions());
    actionService.registerOnBus(true);

    App::instance()->handleArguments(forwarded);

    return app.exec();
}
