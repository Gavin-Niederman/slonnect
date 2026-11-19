import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// M3 slider: track with an active segment and a round handle.
B.Slider {
    id: root
    property color accent: Theme.primary

    implicitHeight: 32
    opacity: enabled ? 1 : 0.38

    background: Rectangle {
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.availableWidth
        height: 4
        radius: 2
        color: Theme.alpha(root.accent, 0.25)
        Rectangle { width: root.visualPosition * parent.width; height: parent.height; radius: 2; color: root.accent }
    }
    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: 16
        height: 16
        radius: 8
        color: root.accent
        Rectangle {
            anchors.centerIn: parent
            width: 40; height: 40; radius: 20
            color: root.accent
            opacity: root.pressed ? Theme.pressOpacity : root.hovered ? Theme.hoverOpacity : 0
        }
    }
}
