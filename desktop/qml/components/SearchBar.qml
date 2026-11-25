import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// M3 search bar: full-pill surface with a leading search icon.
Rectangle {
    id: root
    property alias text: field.text
    property alias placeholder: field.placeholderText
    property alias field: field
    signal accepted()

    implicitHeight: 48
    radius: height / 2
    color: Theme.surfaceContainerHigh

    Icon {
        id: lead
        name: "search"
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
    }
    B.TextField {
        id: field
        anchors.left: lead.right
        anchors.leftMargin: 12
        anchors.right: clear.left
        anchors.verticalCenter: parent.verticalCenter
        background: null
        color: Theme.surfaceFg
        placeholderTextColor: Theme.surfaceVariantFg
        font.pixelSize: Theme.bodyLarge
        selectByMouse: true
        selectionColor: Theme.alpha(Theme.primary, 0.35)
        onAccepted: root.accepted()
        Keys.onEscapePressed: text = ""
    }
    IconButton {
        id: clear
        iconName: "close"
        visible: field.text.length > 0
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        onClicked: field.text = ""
    }
}
