import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// M3 floating action button; becomes "extended" when `text` is set.
B.AbstractButton {
    id: root
    property string iconName
    property bool expanded: true
    readonly property bool extended: text.length > 0 && expanded

    implicitHeight: 56
    implicitWidth: extended ? row.implicitWidth + 40 : 56
    hoverEnabled: true
    Behavior on implicitWidth { NumberAnimation { duration: Theme.durationMedium; easing.type: Easing.OutCubic } }

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.primaryContainer
        Rectangle { // cheap elevation hint instead of a blurred shadow
            anchors.fill: parent; anchors.topMargin: 2; z: -1
            radius: parent.radius; color: Theme.alpha(Theme.scrim, Theme.dark ? 0.5 : 0.18)
        }
        StateLayer { hovered: root.hovered; pressed: root.pressed; tint: Theme.primaryContainerFg }
    }
    contentItem: Item {
        clip: true
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 12
            Icon { name: root.iconName; color: Theme.primaryContainerFg; anchors.verticalCenter: parent.verticalCenter }
            Text {
                visible: root.extended
                text: root.text
                color: Theme.primaryContainerFg
                font.pixelSize: Theme.labelLarge
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
