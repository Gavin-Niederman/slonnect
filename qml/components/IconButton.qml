import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// M3 icon button: style "standard" | "filled" | "tonal" | "outlined".
B.AbstractButton {
    id: root
    property string iconName
    property string style: "standard"
    property int iconSize: 24
    property color iconColor: style === "filled" ? Theme.primaryFg : style === "tonal" ? Theme.secondaryContainerFg
                                                                     : checked ? Theme.primary : Theme.surfaceVariantFg
    property string tooltip

    implicitWidth: 40
    implicitHeight: 40
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1 : 0.38

    background: Rectangle {
        radius: height / 2
        color: root.style === "filled" ? Theme.primary : root.style === "tonal" ? Theme.secondaryContainer : "transparent"
        border.width: root.style === "outlined" ? 1 : 0
        border.color: Theme.outlineVariant
        StateLayer { hovered: root.hovered; pressed: root.pressed; tint: root.iconColor }
        Rectangle {
            anchors.fill: parent; anchors.margins: -2
            radius: height / 2; color: "transparent"
            border.width: 2; border.color: Theme.primary
            visible: root.visualFocus
        }
    }
    contentItem: Icon {
        name: root.iconName
        size: root.iconSize
        color: root.iconColor
        filled: root.checked
    }

    B.ToolTip.visible: hovered && tooltip.length > 0
    B.ToolTip.delay: 600
    B.ToolTip.text: tooltip
}
