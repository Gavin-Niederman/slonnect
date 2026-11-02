import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// M3 navigation rail.
Rectangle {
    id: root
    property var destinations: []
    property int currentIndex: 0
    property var badges: ({})
    default property alias footer: footerColumn.data
    property alias header: headerColumn.data
    signal activated(int index)

    implicitWidth: 88
    color: Theme.surfaceContainer

    Column {
        id: headerColumn
        anchors.top: parent.top
        anchors.topMargin: 16
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 8
    }

    Column {
        anchors.top: headerColumn.bottom
        anchors.topMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 12

        Repeater {
            model: root.destinations
            delegate: B.AbstractButton {
                id: item
                required property var modelData
                required property int index
                readonly property bool selected: root.currentIndex === index
                width: 80
                height: 56
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                onClicked: root.activated(index)

                Rectangle {
                    id: pill
                    width: item.selected ? 56 : 32
                    height: 32
                    radius: 16
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    color: item.selected ? Theme.secondaryContainer : "transparent"
                    Behavior on width { NumberAnimation { duration: Theme.durationMedium; easing.type: Easing.OutCubic } }
                    StateLayer { hovered: item.hovered; pressed: item.pressed; tint: Theme.surfaceFg; width: 56; anchors.fill: undefined; height: 32; anchors.centerIn: parent }
                    Icon {
                        anchors.centerIn: parent
                        name: item.modelData.icon
                        filled: item.selected
                        color: item.selected ? Theme.secondaryContainerFg : Theme.surfaceVariantFg
                    }
                    Rectangle {
                        readonly property int count: root.badges[item.modelData.key] || 0
                        visible: count > 0
                        anchors.right: parent.right; anchors.top: parent.top
                        anchors.rightMargin: item.selected ? 8 : -4; anchors.topMargin: -2
                        width: Math.max(16, badgeText.implicitWidth + 8); height: 16; radius: 8
                        color: Theme.error
                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: parent.count > 99 ? "99+" : parent.count
                            color: Theme.dark ? "#601410" : "#FFFFFF"
                            font.pixelSize: Theme.labelSmall
                            font.weight: Font.Medium
                        }
                    }
                }
                Text {
                    anchors.top: pill.bottom
                    anchors.topMargin: 4
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: item.modelData.title
                    color: item.selected ? Theme.surfaceFg : Theme.surfaceVariantFg
                    font.pixelSize: Theme.labelMedium
                    font.weight: item.selected ? Font.Bold : Font.Medium
                }
            }
        }
    }

    Column {
        id: footerColumn
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 8
    }
}
