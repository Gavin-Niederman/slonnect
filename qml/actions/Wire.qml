import QtQuick
import QtQuick.Shapes
import Slonnect

// A link between two pins, drawn as a horizontal-tangent cubic curve.
Shape {
    id: root
    property real x1
    property real y1
    property real x2
    property real y2
    property color color: Theme.outline
    property real thickness: 2
    property bool dashed: false

    readonly property real bend: Math.max(40, Math.abs(x2 - x1) * 0.5)

    preferredRendererType: Shape.CurveRenderer
    // The shape covers the whole canvas; it never takes input.
    containsMode: Shape.BoundingRectContains
    enabled: false

    ShapePath {
        strokeColor: root.color
        strokeWidth: root.thickness
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        strokeStyle: root.dashed ? ShapePath.DashLine : ShapePath.SolidLine
        dashPattern: [4, 3]
        startX: root.x1
        startY: root.y1
        PathCubic {
            x: root.x2
            y: root.y2
            control1X: root.x1 + root.bend
            control1Y: root.y1
            control2X: root.x2 - root.bend
            control2Y: root.y2
        }
    }
}
