import QtQuick
import Slonnect

// M3 snackbar. Call show(text[, actionText, callback]).
Rectangle {
    id: root
    property string message
    property string actionText
    property var action: null

    function show(text, actionLabel, callback) {
        message = text
        actionText = actionLabel || ""
        action = callback || null
        hideTimer.restart()
        state = "shown"
    }

    width: Math.min(560, row.implicitWidth + 32)
    height: 48
    radius: Theme.radiusXS
    color: Theme.inverseSurface
    opacity: 0
    visible: opacity > 0
    state: ""
    states: State { name: "shown"; PropertyChanges { root.opacity: 1; root.anchors.bottomMargin: 24 } }
    transitions: Transition { NumberAnimation { properties: "opacity,anchors.bottomMargin"; duration: Theme.durationMedium; easing.type: Easing.OutCubic } }

    Timer { id: hideTimer; interval: 4500; onTriggered: root.state = "" }

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 16
        spacing: 16
        Text {
            text: root.message
            color: Theme.inverseSurfaceFg
            font.pixelSize: Theme.bodyMedium
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            width: Math.min(implicitWidth, 420)
        }
        Text {
            visible: root.actionText.length > 0
            text: root.actionText
            color: Theme.inversePrimary
            font.pixelSize: Theme.labelLarge
            font.weight: Font.Medium
            anchors.verticalCenter: parent.verticalCenter
            TapHandler {
                onTapped: { if (root.action) root.action(); root.state = "" }
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
        }
    }
}
