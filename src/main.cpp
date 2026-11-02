#include "app.h"
#include "imagestore.h"
#include "messagetypes.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    // Demo mode keeps its settings apart from real ones.
    QGuiApplication::setOrganizationName(qEnvironmentVariableIsSet("SLONNECT_DEMO") ? QStringLiteral("slonnect-demo") : QStringLiteral("slonnect"));
    QGuiApplication::setApplicationName(QStringLiteral("slonnect"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Slonnect"));
    QGuiApplication::setDesktopFileName(QStringLiteral("dev.slonnect.Slonnect"));
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

    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i)
        App::instance()->handleUrl(args.at(i));

    return app.exec();
}
