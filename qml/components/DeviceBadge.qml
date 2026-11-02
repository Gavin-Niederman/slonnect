import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// Device glyph with a battery ring, shown at the top of the rail.
B.AbstractButton {
    id: root
    readonly property var device: App.device
    implicitWidth: 56
    implicitHeight: 56
    hoverEnabled: true

    B.ToolTip.visible: hovered
    B.ToolTip.delay: 500
    B.ToolTip.text: device ? device.name + (device.reachable ? "" : qsTr(" (disconnected)"))
                             + (device.hasBattery ? " · " + device.charge + "%" + (device.charging ? qsTr(" charging") : "") : "")
                           : qsTr("No device")

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.primaryContainer
        StateLayer { hovered: root.hovered; pressed: root.pressed; tint: Theme.primaryContainerFg }
    }

    contentItem: Item {
        Canvas {
            id: ring
            anchors.centerIn: parent
            width: 44; height: 44
            readonly property real fraction: root.device && root.device.hasBattery ? root.device.charge / 100 : 0
            readonly property color fill: root.device && root.device.charge <= 15 && !root.device.charging ? Theme.error : Theme.primary
            readonly property color track: Theme.alpha(Theme.primaryContainerFg, 0.15)
            onFractionChanged: requestPaint()
            onFillChanged: requestPaint()
            onTrackChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.lineWidth = 3
                ctx.lineCap = "round"
                ctx.strokeStyle = track
                ctx.beginPath(); ctx.arc(22, 22, 20, 0, 2 * Math.PI); ctx.stroke()
                if (fraction > 0) {
                    ctx.strokeStyle = fill
                    ctx.beginPath(); ctx.arc(22, 22, 20, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * fraction); ctx.stroke()
                }
            }
        }
        Icon {
            anchors.centerIn: parent
            name: !root.device ? "phonelink_off"
                : !root.device.reachable ? "mobile_off"
                : root.device.charging ? "battery_charging_full" : (root.device.type === "tablet" ? "tablet_android" : "smartphone")
            filled: true
            color: Theme.primaryContainerFg
        }
    }
}
