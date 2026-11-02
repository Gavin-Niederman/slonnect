#include "clipboardplugin.h"
#include "kdeconnect.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QSettings>
#include <QTimer>

static constexpr int MaxHistory = 50;

ClipboardPlugin::ClipboardPlugin(QObject *parent)
    : DevicePlugin(QStringLiteral("kdeconnect_clipboard"), QStringLiteral("/clipboard"), QStringLiteral("org.kde.kdeconnect.device.clipboard"), parent)
{
    if (qEnvironmentVariableIsSet("SLONNECT_DEMO")) {
        // Never read the real clipboard in demo mode.
        m_history = {QStringLiteral("https://example.com/trail"), QStringLiteral("Meeting notes:\n- projector\n- snacks"), QStringLiteral("+1 555 010 0099")};
        return;
    }
    m_history = QSettings().value(QStringLiteral("clipboard/history")).toStringList();
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ClipboardPlugin::capture);
    capture();
}

void ClipboardPlugin::attached()
{
    subscribe(QStringLiteral("autoShareDisabledChanged"), SLOT(onAutoShareDisabledChanged(bool)));
    kdc::getAll(path(), iface(), this, [this](const QVariantMap &p) { onAutoShareDisabledChanged(p.value(QStringLiteral("isAutoShareDisabled")).toBool()); });
}

void ClipboardPlugin::onAutoShareDisabledChanged(bool disabled)
{
    if (m_autoShare != !disabled) {
        m_autoShare = !disabled;
        Q_EMIT autoShareChanged();
    }
}

void ClipboardPlugin::capture()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasText())
        return;
    // Skip secrets flagged by password managers.
    if (mime->hasFormat(QStringLiteral("x-kde-passwordManagerHint")))
        return;
    const QString text = mime->text();
    if (text.trimmed().isEmpty() || text == m_history.value(0))
        return;
    m_history.removeAll(text);
    m_history.prepend(text);
    while (m_history.size() > MaxHistory)
        m_history.removeLast();
    save();
    Q_EMIT historyChanged();
}

void ClipboardPlugin::sendClipboard()
{
    invoke(QStringLiteral("sendClipboard"));
}

void ClipboardPlugin::sendText(const QString &text)
{
    copy(text);
    // Give kdeconnectd's clipboard watcher a moment to observe the new content.
    QTimer::singleShot(150, this, &ClipboardPlugin::sendClipboard);
}

void ClipboardPlugin::copy(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

void ClipboardPlugin::remove(int index)
{
    if (index < 0 || index >= m_history.size())
        return;
    m_history.removeAt(index);
    save();
    Q_EMIT historyChanged();
}

void ClipboardPlugin::clearHistory()
{
    m_history.clear();
    save();
    Q_EMIT historyChanged();
}

void ClipboardPlugin::save() const
{
    if (qEnvironmentVariableIsSet("SLONNECT_DEMO"))
        return;
    QSettings().setValue(QStringLiteral("clipboard/history"), m_history);
}
