import QtQuick
import Slonnect

// M3 hover / press overlay. Place inside a control and bind `hovered`/`pressed`.
Rectangle {
    property bool hovered
    property bool pressed
    property color tint: Theme.surfaceFg
    anchors.fill: parent
    radius: parent.radius !== undefined ? parent.radius : 0
    color: tint
    opacity: pressed ? Theme.pressOpacity : hovered ? Theme.hoverOpacity : 0
    Behavior on opacity { NumberAnimation { duration: Theme.durationShort } }
}
