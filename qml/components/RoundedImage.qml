import QtQuick
import QtQuick.Effects

// Image clipped to a rounded rectangle. The mask pass only runs once the image
// is loaded, and is skipped on the software renderer (no shader support),
// where the image is shown unclipped.
Item {
    id: root
    property alias source: img.source
    property alias sourceSize: img.sourceSize
    property alias fillMode: img.fillMode
    property alias status: img.status
    property alias implicitImageWidth: img.implicitWidth
    property alias implicitImageHeight: img.implicitHeight
    property real radius: 12
    readonly property bool canMask: GraphicsInfo.api !== GraphicsInfo.Software

    Image {
        id: img
        anchors.fill: parent
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        visible: !root.canMask
    }
    Rectangle {
        id: mask
        anchors.fill: parent
        radius: root.radius
        visible: false
        layer.enabled: root.canMask
    }
    MultiEffect {
        anchors.fill: parent
        visible: root.canMask && img.status === Image.Ready
        source: img
        maskEnabled: true
        maskSource: mask
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
    }
}
