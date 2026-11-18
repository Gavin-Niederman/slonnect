import QtQuick
import Slonnect

// A connection point. Flow pins are diamonds, data pins are circles; filled when linked.
// Pressing starts a wire drag on the canvas; dragging and releasing are forwarded to it.
Item {
    id: root
    required property var canvas
    property string kind: "text"
    property bool linked: false
    signal pressed(point worldPoint)
    signal rightClicked()

    readonly property bool flow: kind === "flow"
    readonly property color color: canvas.pinColor(kind)

    width: 22
    height: 22

    Rectangle {
        anchors.centerIn: parent
        width: root.flow ? 10 : 11
        height: width
        radius: root.flow ? 2 : width / 2
        rotation: root.flow ? 45 : 0
        color: root.linked || area.containsMouse ? root.color : Theme.surfaceContainerLow
        border.width: 2
        border.color: root.color
        scale: area.containsMouse ? 1.25 : 1
        Behavior on scale { NumberAnimation { duration: Theme.durationShort } }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.CrossCursor
        onPressed: mouse => {
            if (mouse.button === Qt.RightButton) {
                root.rightClicked()
                return
            }
            root.pressed(root.mapToItem(root.canvas.world, root.width / 2, root.height / 2))
        }
        onPositionChanged: mouse => {
            if (pressedButtons & Qt.LeftButton)
                root.canvas.moveWire(mapToItem(root.canvas.world, mouse.x, mouse.y))
        }
        onReleased: mouse => {
            if (mouse.button === Qt.LeftButton)
                root.canvas.endWire(mapToItem(root.canvas.world, mouse.x, mouse.y))
        }
        onCanceled: root.canvas.cancelWire()
    }
}
