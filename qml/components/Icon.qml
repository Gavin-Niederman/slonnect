import QtQuick
import QtQuick.Controls.impl
import Slonnect

// Material Icons glyph by name, e.g. Icon { name: "send" }. SVGs are baked into
// the app (qml/icons, see tools/fetch-icons.sh). Outlined by default; `filled`
// switches to the filled style for selected states.
Item {
    id: root
    property string name
    property int size: 24
    property bool filled: false
    property color color: Theme.surfaceVariantFg

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    IconImage {
        anchors.fill: parent
        sourceSize: Qt.size(root.size, root.size)
        source: root.name ? Qt.resolvedUrl("../icons/" + (root.filled ? "filled/" : "outlined/") + root.name + ".svg") : ""
        color: root.color
    }
}
