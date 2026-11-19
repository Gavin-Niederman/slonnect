#include "actionservice.h"
#include "actionstore.h"
#include "app.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcActions)

ActionService::ActionService(ActionStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
}

bool ActionService::registerOnBus(bool queue)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(ObjectPath, this, QDBusConnection::ExportScriptableSlots)) {
        qCWarning(lcActions) << "can't export" << ObjectPath << bus.lastError().message();
        return false;
    }
    const auto reply = bus.interface()->registerService(ServiceName, queue ? QDBusConnectionInterface::QueueService : QDBusConnectionInterface::DontQueueService,
                                                        QDBusConnectionInterface::DontAllowReplacement);
    return reply.isValid() && reply.value() == QDBusConnectionInterface::ServiceRegistered;
}

bool ActionService::forward(const QString &actionId, const QString &deviceId)
{
    auto msg = QDBusMessage::createMethodCall(ServiceName, ObjectPath, QStringLiteral("dev.slonnect.Actions"), QStringLiteral("Run"));
    msg.setArguments({actionId, deviceId});
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 5000);
    return reply.type() == QDBusMessage::ReplyMessage;
}

void ActionService::Run(const QString &actionId, const QString &deviceId)
{
    qCInfo(lcActions) << "run requested:" << actionId << "from" << deviceId;
    m_store->run(actionId, deviceId);
}

QStringList ActionService::List() const
{
    QStringList out;
    for (int i = 0; i < m_store->rowCount(); ++i) {
        const QModelIndex idx = m_store->index(i);
        out << idx.data(ActionStore::IdRole).toString() + QLatin1Char('\t') + idx.data(ActionStore::NameRole).toString();
    }
    return out;
}

bool AppService::registerObject()
{
    return QDBusConnection::sessionBus().registerObject(ObjectPath, this, QDBusConnection::ExportScriptableSlots);
}

bool AppService::forward(const QStringList &args)
{
    auto msg = QDBusMessage::createMethodCall(ActionService::ServiceName, ObjectPath, QStringLiteral("dev.slonnect.App"), QStringLiteral("Activate"));
    msg.setArguments({args});
    // A headless --run-action instance may hold the name but has no /App: that errors
    // out at once and we start a window as usual.
    return QDBusConnection::sessionBus().call(msg, QDBus::Block, 3000).type() == QDBusMessage::ReplyMessage;
}

void AppService::Activate(const QStringList &args)
{
    App::instance()->handleArguments(args);
}
