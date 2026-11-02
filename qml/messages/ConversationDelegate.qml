import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

B.ItemDelegate {
    id: root
    required property var threadId
    required property string title
    required property string body
    required property var date
    required property bool read
    required property bool outgoing
    required property bool multi
    required property string avatar
    required property bool hasAttachment
    property bool selected: false

    function open() { App.sms.openThread(threadId) }

    height: 76
    hoverEnabled: true
    onClicked: open()

    background: Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        radius: Theme.radiusXL
        color: root.selected ? Theme.secondaryContainer : "transparent"
        StateLayer { hovered: root.hovered; pressed: root.pressed; tint: Theme.surfaceFg }
    }

    contentItem: Item {
        Avatar {
            id: avatarItem
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            size: 48
            name: root.title
            source: root.avatar
            fallbackIcon: root.multi ? "group" : "person"
        }
        Text {
            id: titleText
            anchors.left: avatarItem.right
            anchors.leftMargin: 16
            anchors.right: stamp.left
            anchors.rightMargin: 8
            anchors.bottom: parent.verticalCenter
            text: root.title
            elide: Text.ElideRight
            color: Theme.surfaceFg
            font.pixelSize: Theme.bodyLarge
            font.weight: root.read ? Font.Normal : Font.Bold
        }
        Text {
            id: stamp
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.baseline: titleText.baseline
            text: Theme.shortStamp(root.date)
            color: root.read ? Theme.surfaceVariantFg : Theme.primary
            font.pixelSize: Theme.labelMedium
            font.weight: root.read ? Font.Normal : Font.Bold
        }
        Row {
            anchors.left: titleText.left
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.top: parent.verticalCenter
            anchors.topMargin: 2
            spacing: 4
            Icon {
                visible: root.hasAttachment
                name: "attachment"
                size: 16
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                width: parent.width - (root.hasAttachment ? 20 : 0)
                text: (root.outgoing ? qsTr("You: ") : "") + root.body
                elide: Text.ElideRight
                maximumLineCount: 1
                color: root.read ? Theme.surfaceVariantFg : Theme.surfaceFg
                font.pixelSize: Theme.bodyMedium
                font.weight: root.read ? Font.Normal : Font.Medium
            }
        }
    }
}
