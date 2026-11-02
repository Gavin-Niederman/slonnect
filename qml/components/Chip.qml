import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// M3 filter / assist / input chip.
B.AbstractButton {
    id: root
    property string iconName
    property bool removable: false
    signal removeClicked()

    implicitHeight: 32
    implicitWidth: row.implicitWidth + 24
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.radiusS
        color: root.checked ? Theme.secondaryContainer : "transparent"
        border.width: root.checked ? 0 : 1
        border.color: Theme.outlineVariant
        StateLayer { hovered: root.hovered; pressed: root.pressed; tint: Theme.surfaceVariantFg }
    }
    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Icon {
                visible: root.checked || root.iconName.length > 0
                name: root.checked && root.checkable ? "check" : root.iconName
                size: 18
                color: root.checked ? Theme.secondaryContainerFg : Theme.primary
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: root.text
                color: root.checked ? Theme.secondaryContainerFg : Theme.surfaceVariantFg
                font.pixelSize: Theme.labelLarge
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                width: Math.min(implicitWidth, 220)
            }
            Icon {
                visible: root.removable
                name: "close"
                size: 18
                anchors.verticalCenter: parent.verticalCenter
                TapHandler { onTapped: root.removeClicked() }
            }
        }
    }
}
