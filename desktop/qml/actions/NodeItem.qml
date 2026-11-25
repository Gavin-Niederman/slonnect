pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// One node card on the canvas. Row k holds input k on the left and output k on the
// right, so pin positions are a pure function of the index (wires rely on that).
Item {
    id: node
    required property var canvas
    required property ActionGraph graph
    required property string uid
    required property string nodeType
    required property string title
    required property string category
    required property string iconName
    required property string description
    required property real posX
    required property real posY
    required property int nodeWidth
    required property var inputs
    required property var outputs
    required property var values
    required property var linkedInputs
    required property var linkedOutputs
    required property bool isTrigger
    required property string plugin

    readonly property int headerHeight: 38
    readonly property int rowHeight: 32
    readonly property int rows: Math.max(inputs.length, outputs.length)
    readonly property bool selected: canvas.selectedUid === uid
    readonly property string runState: canvas.nodeStates[uid] || ""
    readonly property var colors: canvas.categoryColors(nodeType)
    readonly property bool missingPlugin: plugin.length > 0 && !!App.device && !App.device.loadedPlugins.includes(plugin)
    // Phone nodes the phone said it can't run yet (e.g. Shizuku not started).
    required property string unavailable

    x: posX
    y: posY
    width: nodeWidth
    height: headerHeight + rows * rowHeight + 8
    z: selected || drag.active ? 2 : 1

    function inputIndex(pin) { return inputs.findIndex(p => p.id === pin) }
    function outputIndex(pin) { return outputs.findIndex(p => p.id === pin) }
    function rowCenter(i) { return headerHeight + i * rowHeight + rowHeight / 2 }
    // Pin centre in canvas (world) coordinates; reads x/y so bindings follow drags.
    function pinPoint(side, pin) {
        const i = side === "in" ? inputIndex(pin) : outputIndex(pin)
        return Qt.point(side === "in" ? x : x + width, y + rowCenter(Math.max(0, i)))
    }

    Component.onCompleted: canvas.registerNode(uid, node)
    Component.onDestruction: canvas.unregisterNode(uid, node)

    // Card
    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.radiusM
        color: Theme.surfaceContainerLow
        border.width: node.selected || node.runState === "failed" || node.runState === "running" ? 2 : 1
        border.color: node.runState === "failed" ? Theme.error
                    : node.runState === "running" ? Theme.primary
                    : node.selected ? Theme.primary : Theme.outlineVariant
        Rectangle { // elevation hint
            anchors.fill: parent; anchors.topMargin: 2; z: -1
            radius: parent.radius; color: Theme.alpha(Theme.scrim, Theme.dark ? 0.45 : 0.12)
        }
        SequentialAnimation on opacity {
            running: node.runState === "running"
            loops: Animation.Infinite
            onRunningChanged: if (!running) card.opacity = 1
            NumberAnimation { to: 0.65; duration: 350 }
            NumberAnimation { to: 1; duration: 350 }
        }
    }

    // Header (drag handle)
    Rectangle {
        id: header
        x: 1; y: 1
        width: parent.width - 2
        height: node.headerHeight - 1
        topLeftRadius: Theme.radiusM - 1
        topRightRadius: Theme.radiusM - 1
        color: node.colors[0]

        Icon {
            id: headerIcon
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            name: node.iconName
            size: 18
            color: node.colors[1]
        }
        Text {
            anchors.left: headerIcon.right
            anchors.leftMargin: 8
            anchors.right: headerTools.left
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            text: node.title
            color: node.colors[1]
            font.pixelSize: Theme.labelLarge
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
        Row {
            id: headerTools
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            Icon {
                visible: node.runState === "failed" || node.missingPlugin || node.unavailable.length > 0
                anchors.verticalCenter: parent.verticalCenter
                name: "error"
                size: 18
                color: node.runState === "failed" ? Theme.error : node.colors[1]
                HoverHandler { id: warnHover }
                B.ToolTip.visible: warnHover.hovered
                B.ToolTip.text: node.runState === "failed" ? qsTr("Failed in the last run; see the log")
                              : node.missingPlugin ? qsTr("%1 isn't enabled on the phone").arg(node.plugin.replace("kdeconnect_", ""))
                              : node.unavailable
            }
            IconButton {
                visible: !node.isTrigger && (headerHover.hovered || node.selected)
                width: 28; height: 28
                iconName: "close"
                iconSize: 16
                iconColor: node.colors[1]
                tooltip: qsTr("Delete")
                onClicked: node.graph.removeNode(node.uid)
            }
        }

        HoverHandler { id: headerHover }
        DragHandler {
            id: drag
            target: node
            cursorShape: Qt.ClosedHandCursor
            onActiveChanged: {
                if (active) {
                    node.canvas.selectedUid = node.uid
                    return
                }
                // Snap to an 8 px grid.
                node.x = Math.round(node.x / 8) * 8
                node.y = Math.round(node.y / 8) * 8
                node.graph.moveNode(node.uid, node.x, node.y)
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: { node.canvas.selectedUid = node.uid; nodeMenu.popup() }
        }
    }

    TapHandler {
        onTapped: { node.canvas.selectedUid = node.uid; node.canvas.forceActiveFocus() }
    }
    HoverHandler { id: bodyHover }
    B.ToolTip.visible: bodyHover.hovered && headerHover.hovered && node.description.length > 0
    B.ToolTip.delay: 900
    B.ToolTip.text: node.description

    B.Menu {
        id: nodeMenu
        B.MenuItem {
            text: qsTr("Duplicate")
            enabled: !node.isTrigger
            onTriggered: node.canvas.selectedUid = node.graph.duplicateNode(node.uid)
        }
        B.MenuItem {
            text: qsTr("Disconnect all")
            onTriggered: {
                node.inputs.forEach(p => node.graph.disconnectPin(node.uid, p.id, true))
                node.outputs.forEach(p => node.graph.disconnectPin(node.uid, p.id, false))
            }
        }
        B.MenuItem {
            text: qsTr("Delete")
            enabled: !node.isTrigger
            onTriggered: node.graph.removeNode(node.uid)
        }
    }

    // Pin rows
    Repeater {
        model: node.rows
        delegate: Item {
            id: row
            required property int index
            readonly property var input: index < node.inputs.length ? node.inputs[index] : null
            readonly property var output: index < node.outputs.length ? node.outputs[index] : null
            readonly property bool inputLinked: !!input && node.linkedInputs.includes(input.id)
            readonly property bool outputLinked: !!output && node.linkedOutputs.includes(output.id)
            readonly property bool showEditor: !!input && input.kind !== "flow" && !inputLinked

            y: node.headerHeight + index * node.rowHeight
            width: node.width
            height: node.rowHeight

            // --- input side ---
            Pin {
                visible: !!row.input
                x: -width / 2
                anchors.verticalCenter: parent.verticalCenter
                kind: row.input ? row.input.kind : "text"
                linked: row.inputLinked
                canvas: node.canvas
                onPressed: point => {
                    const pin = row.input.id
                    const existing = node.graph.linkInto(node.uid, pin)
                    if (existing.from) {
                        // Pick the wire up from this end and carry it elsewhere.
                        node.graph.disconnectPin(node.uid, pin, true)
                        const src = node.canvas.nodeItems[existing.from]
                        node.canvas.beginWire(existing.from, existing.fromPin, "out", existing.flow, src ? src.pinPoint("out", existing.fromPin) : point)
                    } else {
                        node.canvas.beginWire(node.uid, pin, "in", row.input.kind === "flow", point)
                    }
                }
                onRightClicked: node.graph.disconnectPin(node.uid, row.input.id, true)
            }
            Text {
                id: inLabel
                visible: !!row.input && row.input.label.length > 0
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                width: row.showEditor ? Math.min(implicitWidth, 76) : Math.min(implicitWidth, (row.output ? node.width / 2 : node.width) - 20)
                text: row.input ? row.input.label : ""
                color: Theme.surfaceVariantFg
                font.pixelSize: Theme.labelMedium
                elide: Text.ElideRight
            }
            PinField {
                visible: row.showEditor
                anchors.left: inLabel.visible ? inLabel.right : parent.left
                anchors.leftMargin: inLabel.visible ? 8 : 12
                anchors.right: parent.right
                anchors.rightMargin: row.output ? Math.max(outLabel.implicitWidth + 20, node.width * 0.32) : 12
                anchors.verticalCenter: parent.verticalCenter
                pin: row.input || ({ kind: "text" })
                value: row.input ? node.values[row.input.id] : undefined
                onEdited: v => node.graph.setValue(node.uid, row.input.id, v)
                context: node.values
                // Prefill related inputs the user hasn't set (e.g. args for a chosen method).
                onFill: values => {
                    for (const k in values) {
                        const cur = node.values[k]
                        if (cur === undefined || cur === null || String(cur).trim() === "" || String(cur).trim() === "[]")
                            node.graph.setValue(node.uid, k, values[k])
                    }
                }
            }

            // --- output side ---
            Text {
                id: outLabel
                visible: !!row.output && row.output.label.length > 0
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: row.output ? row.output.label : ""
                color: Theme.surfaceVariantFg
                font.pixelSize: Theme.labelMedium
            }
            Pin {
                visible: !!row.output
                x: parent.width - width / 2
                anchors.verticalCenter: parent.verticalCenter
                kind: row.output ? row.output.kind : "text"
                linked: row.outputLinked
                canvas: node.canvas
                onPressed: point => node.canvas.beginWire(node.uid, row.output.id, "out", row.output.kind === "flow", point)
                onRightClicked: node.graph.disconnectPin(node.uid, row.output.id, false)
            }
        }
    }
}
