import QtQuick
import Slonnect

// M3 card container: "filled" | "outlined" | "elevated".
Rectangle {
    property string style: "filled"
    property int padding: 16
    default property alias content: inner.data

    radius: Theme.radiusL
    color: style === "filled" ? Theme.surfaceContainerHighest : style === "elevated" ? Theme.surfaceContainerLow : Theme.surface
    border.width: style === "outlined" ? 1 : 0
    border.color: Theme.outlineVariant
    implicitHeight: inner.childrenRect.height + 2 * padding

    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: parent.padding
    }
}
