pragma Singleton
import QtQuick

// Navigation destinations. Each feature is backed by KDE Connect plugins
// (see src/deviceplugin.h). To add one: write a DevicePlugin subclass, expose it
// from App, add a page under pages/, and list it here. `rail: false` keeps a page
// out of the navigation rail (it's reached some other way).
QtObject {
    readonly property var destinations: [
        { key: "messages",      title: qsTr("Messages"),      icon: "chat",          page: "pages/MessagesPage.qml",      plugin: "kdeconnect_sms" },
        { key: "notifications", title: qsTr("Alerts"),        icon: "notifications", page: "pages/NotificationsPage.qml", plugin: "kdeconnect_notifications" },
        { key: "files",         title: qsTr("Files"),         icon: "folder_open",   page: "pages/FilesPage.qml",         plugin: "kdeconnect_share" },
        { key: "clipboard",     title: qsTr("Clipboard"),     icon: "content_paste", page: "pages/ClipboardPage.qml",     plugin: "kdeconnect_clipboard" },
        { key: "media",         title: qsTr("Media"),         icon: "play_circle",   page: "pages/MediaPage.qml",         plugin: "kdeconnect_mprisremote" },
        { key: "remote",        title: qsTr("Remote"),        icon: "touch_app",     page: "pages/RemotePage.qml",        plugin: "kdeconnect_remotecontrol" },
        { key: "actions",       title: qsTr("Actions"),       icon: "bolt",          page: "pages/ActionsPage.qml",       plugin: "kdeconnect_runcommand" },
        { key: "device",        title: qsTr("Device"),        icon: "smartphone",    page: "pages/DevicePage.qml",        plugin: "" },
        { key: "settings",      title: qsTr("Settings"),      icon: "settings",      page: "pages/SettingsPage.qml",      plugin: "", rail: false }
    ]

    // Every KDE Connect plugin, for the plugin list in Settings. Titles and
    // descriptions follow KDE Connect's own metadata, from this computer's side.
    readonly property var plugins: ({
        "kdeconnect_battery":              { title: qsTr("Battery monitor"),           icon: "battery_full",        description: qsTr("Share the battery status between devices") },
        "kdeconnect_clipboard":            { title: qsTr("Clipboard sync"),            icon: "content_paste",       description: qsTr("Share the clipboard between devices") },
        "kdeconnect_connectivity_report":  { title: qsTr("Connectivity monitor"),      icon: "signal_cellular_alt", description: qsTr("Show the phone's mobile network signal") },
        "kdeconnect_contacts":             { title: qsTr("Sync contacts"),             icon: "contacts",            description: qsTr("Get the phone's contacts for names and photos") },
        "kdeconnect_digitizer":            { title: qsTr("Drawing tablet"),            icon: "edit",                description: qsTr("Use the device as a pressure-sensitive drawing tablet") },
        "kdeconnect_findmyphone":          { title: qsTr("Find my phone"),             icon: "ring_volume",         description: qsTr("Make the phone ring so you can find it") },
        "kdeconnect_findthisdevice":       { title: qsTr("Find this computer"),        icon: "notifications_active", description: qsTr("Let the phone make this computer play a sound") },
        "kdeconnect_lockdevice":           { title: qsTr("Lock device"),               icon: "lock",                description: qsTr("Lock and unlock each other's screen") },
        "kdeconnect_mmtelephony":          { title: qsTr("ModemManager calls"),        icon: "call",                description: qsTr("Notify about calls on this computer's modem") },
        "kdeconnect_mousepad":             { title: qsTr("Receive remote input"),      icon: "mouse",               description: qsTr("Use the phone as a touchpad and keyboard for this computer") },
        "kdeconnect_mpriscontrol":         { title: qsTr("Media control receiver"),    icon: "play_circle",         description: qsTr("Let the phone control players on this computer") },
        "kdeconnect_mprisremote":          { title: qsTr("Media control"),             icon: "music_note",          description: qsTr("Control the phone's media players") },
        "kdeconnect_notifications":        { title: qsTr("Receive notifications"),     icon: "notifications",       description: qsTr("Show the phone's notifications here and keep them in sync") },
        "kdeconnect_pausemusic":           { title: qsTr("Pause media during calls"),  icon: "pause",               description: qsTr("Pause music and videos while the phone rings or you talk") },
        "kdeconnect_ping":                 { title: qsTr("Ping"),                      icon: "notifications_active", description: qsTr("Send and receive pings") },
        "kdeconnect_presenter":            { title: qsTr("Presenter"),                 icon: "co_present",          description: qsTr("Use the phone as a presentation remote and laser pointer") },
        "kdeconnect_remotecommands":       { title: qsTr("Run remote commands"),       icon: "terminal",            description: qsTr("Trigger commands defined on the other device") },
        "kdeconnect_remotecontrol":        { title: qsTr("Send remote mouse input"),   icon: "touch_app",           description: qsTr("Control the other device's pointer from here") },
        "kdeconnect_remotekeyboard":       { title: qsTr("Send remote keyboard input"), icon: "keyboard",           description: qsTr("Type on the other device from here") },
        "kdeconnect_remotesystemvolume":   { title: qsTr("Remote volume control"),     icon: "speaker",             description: qsTr("Control the other device's volume") },
        "kdeconnect_runcommand":           { title: qsTr("Run commands"),              icon: "bolt",                description: qsTr("Commands the phone can run here (Slonnect's Actions)") },
        "kdeconnect_screensaver_inhibit":  { title: qsTr("Inhibit screensaver"),       icon: "fullscreen",          description: qsTr("Keep the screensaver off while the device is connected") },
        "kdeconnect_sendnotifications":    { title: qsTr("Send notifications"),        icon: "notifications_off",   description: qsTr("Mirror this computer's notifications to the device") },
        "kdeconnect_sftp":                 { title: qsTr("Browse files"),              icon: "folder",              description: qsTr("Browse the phone's storage over SFTP") },
        "kdeconnect_share":                { title: qsTr("Share and receive"),         icon: "share",               description: qsTr("Send and receive files, links and text") },
        "kdeconnect_shareinputdevices":    { title: qsTr("Share input devices"),       icon: "devices",             description: qsTr("Move this computer's mouse and keyboard over to the device") },
        "kdeconnect_shareinputdevicesremote": { title: qsTr("Shared input receiver"),  icon: "devices",             description: qsTr("Accept mouse and keyboard shared from the device") },
        "kdeconnect_sms":                  { title: qsTr("Messages"),                  icon: "chat",                description: qsTr("Send and receive SMS, MMS and RCS") },
        "kdeconnect_systemvolume":         { title: qsTr("Receive volume control"),    icon: "volume_up",           description: qsTr("Let the phone control this computer's volume") },
        "kdeconnect_telephony":            { title: qsTr("Call notifications"),        icon: "call",                description: qsTr("Notify about incoming and missed calls") },
        "kdeconnect_virtualmonitor":       { title: qsTr("Virtual display"),           icon: "cast",                description: qsTr("Use the device as an extra screen") }
    })
    function pluginInfo(id) {
        return plugins[id] || { title: id.replace(/^kdeconnect_/, "").replace(/_/g, " "), icon: "extension", description: "" }
    }
}
