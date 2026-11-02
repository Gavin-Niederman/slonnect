import QtQuick
import Slonnect

Column {
    id: root
    property string iconName
    property string title
    property string text
    default property alias actions: actionRow.data

    spacing: 12
    width: Math.min(parent ? parent.width - 48 : 360, 380)

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: 96; height: 96; radius: 48
        color: Theme.secondaryContainer
        Icon { anchors.centerIn: parent; name: root.iconName; size: 48; color: Theme.secondaryContainerFg }
    }
    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: root.title
        color: Theme.surfaceFg
        font.pixelSize: Theme.titleLarge
        wrapMode: Text.Wrap
    }
    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        visible: text.length > 0
        text: root.text
        color: Theme.surfaceVariantFg
        font.pixelSize: Theme.bodyMedium
        wrapMode: Text.Wrap
        lineHeight: 1.2
    }
    Row {
        id: actionRow
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 8
        topPadding: 8
    }
}
