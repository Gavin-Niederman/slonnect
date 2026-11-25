import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import Slonnect

ApplicationWindow {
    id: window
    width: 1180
    height: 760
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: App.device ? "Slonnect — " + App.device.name : "Slonnect"

    Material.theme: Theme.dark ? Material.Dark : Material.Light
    Material.accent: Theme.primary
    Material.primary: Theme.primary
    Material.background: Theme.surface
    Material.foreground: Theme.surfaceFg
    Material.roundedScale: Material.ExtraLargeScale
    color: Theme.surface

    property int currentPage: 0
    // Plugin whose settings to show when Settings opens (from --plugin-config).
    property string focusPlugin
    property var visited: ({ 0: true })
    readonly property var destinations: Features.destinations

    function go(key) {
        for (let i = 0; i < destinations.length; ++i)
            if (destinations[i].key === key) { currentPage = i; return }
    }
    // Assign a fresh object so bindings on `visited` re-evaluate.
    onCurrentPageChanged: { const v = Object.assign({}, visited); v[currentPage] = true; visited = v }

    // Ctrl+1..5 jumps between destinations.
    Instantiator {
        model: window.destinations.length
        delegate: Shortcut {
            required property int index
            sequence: "Ctrl+" + (index + 1)
            onActivated: window.currentPage = index
        }
    }

    Shortcut { sequence: "Ctrl+,"; onActivated: window.go("settings") }

    Connections {
        target: App
        function onShowMessages() { window.go("messages") }
        // Another launch (kdeconnect-app/-sms style flags, sms: links) handed us its arguments.
        function onActivateRequested(page, plugin) {
            if (plugin)
                window.focusPlugin = plugin
            if (page)
                window.go(page)
            window.show()
            window.raise()
            window.requestActivate()
        }
    }
    Connections {
        target: App.devices
        function onPairingFailed(name, error) { window.toast(qsTr("Pairing with %1 failed: %2").arg(name).arg(error)) }
    }
    Connections {
        target: App.virtualMonitor
        function onFailed(error) { window.toast(error) }
    }
    Connections {
        target: App.device
        function onBrowseFailed(error) { window.toast(qsTr("Couldn't open phone storage: %1").arg(error)) }
    }
    Connections {
        target: App.mirror
        function onFailed(error) { window.toast(error) }
    }
    Connections {
        target: App.share
        function onFileReceived(name) {
            snackbar.show(qsTr("Received %1").arg(name), qsTr("Open folder"), () => App.share.openIncomingFolder())
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        NavRail {
            Layout.fillHeight: true
            destinations: window.destinations
            currentIndex: window.currentPage
            badges: ({ notifications: App.notifications.count, device: App.devices.pairingRequest ? 1 : 0 })
            onActivated: index => window.currentPage = index
            header: DeviceBadge { onClicked: window.go("device") }

            IconButton {
                iconName: "settings"
                tooltip: qsTr("Settings (Ctrl+,)")
                checked: window.destinations[window.currentPage].key === "settings"
                onClicked: window.go("settings")
            }
            IconButton {
                iconName: Theme.mode === "dark" ? "dark_mode" : Theme.mode === "light" ? "light_mode" : "brightness_auto"
                tooltip: qsTr("Theme: %1").arg(Theme.mode)
                onClicked: Theme.setMode(Theme.mode === "system" ? "light" : Theme.mode === "light" ? "dark" : "system")
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            StatusBanner { Layout.fillWidth: true }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.currentPage

                Repeater {
                    model: window.destinations
                    // Pages are created on first visit and then kept alive so
                    // switching back is instant and keeps scroll positions.
                    delegate: Loader {
                        required property var modelData
                        required property int index
                        active: window.visited[index] === true
                        asynchronous: index !== window.currentPage
                        source: Qt.resolvedUrl(modelData.page)
                        onLoaded: item.window = window
                    }
                }
            }
        }
    }

    Snackbar {
        id: snackbar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 0
        z: 100
    }
    function toast(text, actionText, callback) { snackbar.show(text, actionText, callback) }
}
