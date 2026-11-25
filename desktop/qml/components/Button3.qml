import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// M3 common button: style "filled" | "tonal" | "outlined" | "text" | "elevated".
B.AbstractButton {
    id: root
    property string style: "filled"
    property string iconName
    property color fg: style === "filled" ? Theme.primaryFg : style === "tonal" ? Theme.secondaryContainerFg : Theme.primary

    implicitHeight: 40
    implicitWidth: Math.max(64, row.implicitWidth + (style === "text" ? 24 : 48) - (icon ? 8 : 0))
    hoverEnabled: true
    opacity: enabled ? 1 : 0.38

    background: Rectangle {
        radius: height / 2
        color: root.style === "filled" ? Theme.primary
             : root.style === "tonal" ? Theme.secondaryContainer
             : root.style === "elevated" ? Theme.surfaceContainerLow : "transparent"
        border.width: root.style === "outlined" ? 1 : 0
        border.color: Theme.outline
        StateLayer { hovered: root.hovered; pressed: root.pressed; tint: root.fg }
        Rectangle {
            anchors.fill: parent; anchors.margins: -3
            radius: height / 2; color: "transparent"
            border.width: 2; border.color: Theme.primary
            visible: root.visualFocus
        }
    }
    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Icon { name: root.iconName; size: 18; color: root.fg; visible: root.iconName.length > 0; anchors.verticalCenter: parent.verticalCenter }
            Text {
                text: root.text
                color: root.fg
                font.pixelSize: Theme.labelLarge
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
