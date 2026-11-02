import QtQuick
import Slonnect

// Shown above every page while the daemon or device isn't usable.
Rectangle {
    id: root
    readonly property var device: App.device
    readonly property string message: !App.devices.daemonRunning ? qsTr("KDE Connect isn't running. Start kdeconnectd to continue.")
                                    : !device ? qsTr("No paired device. Pair your phone from the Device page.")
                                    : !device.reachable ? qsTr("%1 is offline. Showing cached data.").arg(device.name)
                                    : ""
    visible: message.length > 0
    implicitHeight: visible ? 48 : 0
    color: !App.devices.daemonRunning ? Theme.errorContainer : Theme.secondaryContainer

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 24
        spacing: 12
        Icon {
            name: !App.devices.daemonRunning ? "error" : "cloud_off"
            color: !App.devices.daemonRunning ? Theme.errorContainerFg : Theme.secondaryContainerFg
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.message
            color: !App.devices.daemonRunning ? Theme.errorContainerFg : Theme.secondaryContainerFg
            font.pixelSize: Theme.bodyMedium
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
