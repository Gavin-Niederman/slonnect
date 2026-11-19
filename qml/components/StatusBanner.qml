import QtQuick
import Slonnect

// Shown above every page while the daemon or device isn't usable, or while a
// device is asking to pair.
Rectangle {
    id: root
    readonly property var device: App.device
    readonly property var request: App.devices.daemonRunning ? App.devices.pairingRequest : null
    readonly property bool failed: !App.devices.daemonRunning
    readonly property string message: failed ? qsTr("KDE Connect isn't running. Start kdeconnectd to continue.")
                                    : request ? qsTr("%1 wants to pair. Check that both screens show the key %2.").arg(request.name).arg(request.verificationKey)
                                    : !device ? qsTr("No paired device. Pair your phone from the Device page.")
                                    : !device.reachable ? qsTr("%1 is offline. Showing cached data.").arg(device.name)
                                    : ""
    readonly property color fg: failed ? Theme.errorContainerFg : request ? Theme.tertiaryContainerFg : Theme.secondaryContainerFg
    visible: message.length > 0
    implicitHeight: visible ? Math.max(48, label.implicitHeight + 16) : 0
    color: failed ? Theme.errorContainer : request ? Theme.tertiaryContainer : Theme.secondaryContainer

    Icon {
        id: icon
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 24
        name: root.failed ? "error" : root.request ? "key" : "cloud_off"
        color: root.fg
    }
    Text {
        id: label
        anchors.left: icon.right
        anchors.leftMargin: 12
        anchors.right: buttons.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: root.message
        color: root.fg
        font.pixelSize: Theme.bodyMedium
        wrapMode: Text.Wrap
    }
    Row {
        id: buttons
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        visible: !!root.request
        width: visible ? implicitWidth : 0
        Button3 { style: "text"; fg: root.fg; text: qsTr("Reject"); onClicked: root.request.cancelPairing() }
        Button3 { style: "filled"; text: qsTr("Accept"); onClicked: root.request.acceptPairing() }
    }
}
