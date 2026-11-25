import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// M3 switch.
B.AbstractButton {
    id: root
    checkable: true
    hoverEnabled: true
    implicitWidth: 52
    implicitHeight: 32
    opacity: enabled ? 1 : 0.38

    background: Rectangle {
        radius: height / 2
        color: root.checked ? Theme.primary : Theme.surfaceContainerHighest
        border.width: root.checked ? 0 : 2
        border.color: Theme.outline
        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
        Rectangle {
            anchors.fill: parent; anchors.margins: -3
            radius: height / 2; color: "transparent"
            border.width: 2; border.color: Theme.primary
            visible: root.visualFocus
        }
    }
    contentItem: Item {
        Rectangle {
            readonly property real d: root.checked ? 24 : root.pressed ? 28 : 16
            width: d
            height: d
            radius: d / 2
            anchors.verticalCenter: parent.verticalCenter
            x: root.checked ? parent.width - width - 4 : (root.pressed ? 2 : 8)
            color: root.checked ? Theme.primaryFg : Theme.outline
            Behavior on x { NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic } }
            Behavior on width { NumberAnimation { duration: Theme.durationShort } }
            Rectangle {
                anchors.centerIn: parent
                width: 40; height: 40; radius: 20
                color: root.checked ? Theme.primary : Theme.surfaceFg
                opacity: root.pressed ? Theme.pressOpacity : root.hovered ? Theme.hoverOpacity : 0
            }
        }
    }
}
