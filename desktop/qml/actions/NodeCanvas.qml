pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Slonnect

// Pannable, zoomable node editor surface for one ActionGraph.
//   drag empty space: pan · wheel: zoom · drag a pin: connect · right-click a pin: disconnect
//   right-click / double-click empty space: add node · Delete: remove selected node
Item {
    id: canvas
    required property ActionGraph graph
    property var nodeStates: ({})      // uid -> "running" | "done" | "failed"
    property string selectedUid: ""
    property var nodeItems: ({})       // uid -> NodeItem
    readonly property alias world: world
    readonly property real zoom: world.scale

    // Asks the page to show the node palette; `worldPos` is where the node should go.
    signal paletteRequested(point worldPos, point viewPos)

    clip: true
    focus: true
    activeFocusOnTab: true

    // --- colours -------------------------------------------------------------------
    // [background, foreground] for a node type, by its id prefix ("phone.ring" -> phone).
    function categoryColors(typeId) {
        const p = Theme.avatarPalette
        switch (typeId.split(".")[0]) {
        case "trigger": return [Theme.primary, Theme.primaryFg]
        case "phone":
        case "mobile": return [Theme.tertiaryContainer, Theme.tertiaryContainerFg]
        case "desktop": return [p[3][0], p[3][1]]
        case "logic": return [p[4][0], p[4][1]]
        default: return [p[5][0], p[5][1]]
        }
    }
    function pinColor(kind) {
        switch (kind) {
        case "flow": return Theme.surfaceFg
        case "number": return Qt.color(Theme.dark ? "#4FD8C4" : "#00796B")
        case "bool": return Qt.color(Theme.dark ? "#FFB95C" : "#A15C00")
        case "choice": return Theme.secondary
        default: return Qt.color(Theme.dark ? "#F2A8C4" : "#A3365F")
        }
    }

    // --- node registry (wires look nodes up by uid) ---------------------------------
    function registerNode(uid, item) {
        const m = Object.assign({}, nodeItems)
        m[uid] = item
        nodeItems = m
    }
    function unregisterNode(uid, item) {
        if (nodeItems[uid] !== item)
            return
        const m = Object.assign({}, nodeItems)
        delete m[uid]
        nodeItems = m
    }

    // --- view ----------------------------------------------------------------------
    function viewToWorld(p) { return Qt.point((p.x - world.x) / world.scale, (p.y - world.y) / world.scale) }
    function viewCenterWorld() { return viewToWorld(Qt.point(width / 2, height / 2)) }
    function zoomAt(viewPoint, factor) {
        const s = Math.max(0.3, Math.min(2.0, world.scale * factor))
        const k = s / world.scale
        world.x = viewPoint.x - (viewPoint.x - world.x) * k
        world.y = viewPoint.y - (viewPoint.y - world.y) * k
        world.scale = s
    }
    function fit() {
        const b = graph.bounds()
        if (!b.width || width <= 0 || height <= 0)
            return
        const margin = 48
        const s = Math.max(0.4, Math.min(1.25, (width - 2 * margin) / b.width, (height - 2 * margin) / b.height))
        world.scale = s
        world.x = (width - b.width * s) / 2 - b.x * s
        world.y = (height - b.height * s) / 2 - b.y * s
    }

    // --- wire dragging ---------------------------------------------------------------
    // { uid, pin, side: "out" | "in", flow, start: point, end: point }
    property var pending: null
    property var hoverTarget: null     // { uid, pin } a valid drop target under the cursor

    function beginWire(uid, pin, side, flow, start) {
        selectedUid = ""
        pending = { uid: uid, pin: pin, side: side, flow: flow, start: start, end: start }
    }
    function moveWire(p) {
        if (!pending)
            return
        pending = Object.assign({}, pending, { end: p })
        hoverTarget = findTarget(p)
    }
    function cancelWire() { pending = null; hoverTarget = null }
    function endWire(p) {
        if (!pending)
            return
        const t = findTarget(p)
        if (t) {
            if (pending.side === "out")
                graph.connectPins(pending.uid, pending.pin, t.uid, t.pin)
            else
                graph.connectPins(t.uid, t.pin, pending.uid, pending.pin)
        }
        cancelWire()
    }
    // The closest compatible pin on the other side, within reach of `p`, or whose
    // row `p` is over.
    function findTarget(p) {
        if (!pending)
            return null
        const want = pending.side === "out" ? "in" : "out"
        const reach = 22 / world.scale
        let best = null, bestDist = Infinity
        for (const uid in nodeItems) {
            const item = nodeItems[uid]
            if (uid === pending.uid)
                continue
            const pins = want === "in" ? item.inputs : item.outputs
            const inside = p.x >= item.x - reach && p.x <= item.x + item.width + reach && p.y >= item.y && p.y <= item.y + item.height
            for (let i = 0; i < pins.length; ++i) {
                if ((pins[i].kind === "flow") !== pending.flow)
                    continue
                const pt = item.pinPoint(want, pins[i].id)
                const d = Math.hypot(pt.x - p.x, pt.y - p.y)
                const overRow = inside && Math.abs(pt.y - p.y) <= item.rowHeight / 2
                const score = d <= reach ? d : overRow ? reach + Math.abs(pt.x - p.x) / 10 : Infinity
                if (score < bestDist) {
                    const ok = want === "in" ? graph.canConnect(pending.uid, pending.pin, uid, pins[i].id)
                                             : graph.canConnect(uid, pins[i].id, pending.uid, pending.pin)
                    if (ok) { best = { uid: uid, pin: pins[i].id }; bestDist = score }
                }
            }
        }
        return best
    }

    Keys.onDeletePressed: if (selectedUid) { graph.removeNode(selectedUid); selectedUid = "" }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Backspace && selectedUid) { graph.removeNode(selectedUid); selectedUid = ""; event.accepted = true }
        else if (event.key === Qt.Key_D && (event.modifiers & Qt.ControlModifier) && selectedUid) { selectedUid = graph.duplicateNode(selectedUid); event.accepted = true }
        else if (event.key === Qt.Key_Escape) { selectedUid = ""; cancelWire() }
    }

    // --- background: dot grid, pan, palette -------------------------------------------
    Rectangle { anchors.fill: parent; color: Theme.surfaceContainerLowest }

    Canvas {
        id: grid
        anchors.fill: parent
        readonly property real step: 24 * world.scale
        readonly property real ox: ((world.x % step) + step) % step
        readonly property real oy: ((world.y % step) + step) % step
        readonly property color dot: Theme.outlineVariant
        onStepChanged: requestPaint()
        onOxChanged: requestPaint()
        onOyChanged: requestPaint()
        onDotChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = dot
            const r = Math.max(0.8, 1.2 * world.scale)
            for (let x = ox; x < width; x += step)
                for (let y = oy; y < height; y += step)
                    ctx.fillRect(x - r / 2, y - r / 2, r, r)
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        property point last
        property bool moved: false
        cursorShape: pressed && moved ? Qt.ClosedHandCursor : Qt.ArrowCursor
        onPressed: mouse => { last = Qt.point(mouse.x, mouse.y); moved = false; canvas.forceActiveFocus() }
        onPositionChanged: mouse => {
            if (!(pressedButtons & (Qt.LeftButton | Qt.MiddleButton)))
                return
            world.x += mouse.x - last.x
            world.y += mouse.y - last.y
            moved = moved || Math.abs(mouse.x - last.x) + Math.abs(mouse.y - last.y) > 0
            last = Qt.point(mouse.x, mouse.y)
        }
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton)
                canvas.paletteRequested(canvas.viewToWorld(Qt.point(mouse.x, mouse.y)), Qt.point(mouse.x, mouse.y))
            else if (!moved)
                canvas.selectedUid = ""
        }
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton)
                canvas.paletteRequested(canvas.viewToWorld(Qt.point(mouse.x, mouse.y)), Qt.point(mouse.x, mouse.y))
        }
    }
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            // Ctrl+wheel or a mouse wheel zooms; two-finger touchpad scrolling pans.
            if ((event.modifiers & Qt.ControlModifier) || event.pixelDelta.y === 0) {
                canvas.zoomAt(Qt.point(event.x, event.y), Math.pow(1.0015, event.angleDelta.y))
            } else {
                world.x += event.pixelDelta.x
                world.y += event.pixelDelta.y
            }
        }
    }
    PinchHandler {
        target: null
        property real startScale: 1
        onActiveChanged: if (active) startScale = world.scale
        onActiveScaleChanged: canvas.zoomAt(centroid.position, startScale * activeScale / world.scale)
    }

    // --- world: wires under nodes ---------------------------------------------------------
    Item {
        id: world
        transformOrigin: Item.TopLeft
        width: 1; height: 1

        Repeater {
            model: canvas.graph.links
            delegate: Wire {
                id: wire
                required property var modelData
                readonly property Item a: canvas.nodeItems[modelData.from] || null
                readonly property Item b: canvas.nodeItems[modelData.to] || null
                readonly property point p1: a ? a.pinPoint("out", modelData.fromPin) : Qt.point(0, 0)
                readonly property point p2: b ? b.pinPoint("in", modelData.toPin) : Qt.point(0, 0)
                readonly property bool live: canvas.nodeStates[modelData.from] === "done" && !!canvas.nodeStates[modelData.to]
                visible: !!a && !!b
                x1: p1.x; y1: p1.y; x2: p2.x; y2: p2.y
                thickness: modelData.flow ? 3 : 2
                color: live ? Theme.primary : modelData.flow ? Theme.alpha(Theme.surfaceFg, 0.7) : Theme.alpha(canvas.pinColor("text"), 0.75)
            }
        }

        // Wire being dragged.
        Wire {
            visible: !!canvas.pending
            readonly property point from: canvas.pending ? (canvas.pending.side === "out" ? canvas.pending.start : canvas.pending.end) : Qt.point(0, 0)
            readonly property point to: canvas.pending ? (canvas.pending.side === "out" ? canvas.pending.end : canvas.pending.start) : Qt.point(0, 0)
            x1: from.x; y1: from.y; x2: to.x; y2: to.y
            dashed: !canvas.hoverTarget
            thickness: 2.5
            color: Theme.primary
        }

        Repeater {
            model: canvas.graph
            delegate: NodeItem {
                canvas: canvas
                graph: canvas.graph
            }
        }
    }
}
