pragma Singleton
import QtQuick

// Navigation destinations. Each feature is backed by one KDE Connect plugin
// (see src/deviceplugin.h). To add one: write a DevicePlugin subclass, expose it
// from App, add a page under pages/, and list it here.
QtObject {
    readonly property var destinations: [
        { key: "messages",  title: qsTr("Messages"),  icon: "chat",            page: "pages/MessagesPage.qml",  plugin: "kdeconnect_sms" },
        { key: "files",     title: qsTr("Files"),     icon: "folder_open",     page: "pages/FilesPage.qml",     plugin: "kdeconnect_share" },
        { key: "clipboard", title: qsTr("Clipboard"), icon: "content_paste",   page: "pages/ClipboardPage.qml", plugin: "kdeconnect_clipboard" },
        { key: "media",     title: qsTr("Media"),     icon: "play_circle",     page: "pages/MediaPage.qml",     plugin: "kdeconnect_mprisremote" },
        { key: "actions",   title: qsTr("Actions"),   icon: "bolt",            page: "pages/ActionsPage.qml",  plugin: "kdeconnect_runcommand" },
        { key: "device",    title: qsTr("Device"),    icon: "smartphone",      page: "pages/DevicePage.qml",    plugin: "" }
    ]

    // Plugins the device may offer that Slonnect does not surface yet.
    // Each is a small, self-contained addition on top of the existing plumbing.
    readonly property var planned: [
        { plugin: "kdeconnect_notifications",   title: qsTr("Notifications"),       icon: "notifications" },
        { plugin: "kdeconnect_telephony",       title: qsTr("Call alerts"),         icon: "call" },
        { plugin: "kdeconnect_mousepad",        title: qsTr("Remote input"),        icon: "mouse" },
        { plugin: "kdeconnect_remotekeyboard",  title: qsTr("Remote keyboard"),     icon: "keyboard" },
        { plugin: "kdeconnect_remotecommands",  title: qsTr("Remote commands"),     icon: "terminal" },
        { plugin: "kdeconnect_presenter",       title: qsTr("Presenter"),           icon: "co_present" },
        { plugin: "kdeconnect_connectivity_report", title: qsTr("Signal strength"), icon: "signal_cellular_alt" },
        { plugin: "kdeconnect_systemvolume",    title: qsTr("Desktop volume"),      icon: "volume_up" },
        { plugin: "kdeconnect_virtualmonitor",  title: qsTr("Virtual display"),     icon: "cast" }
    ]
}
