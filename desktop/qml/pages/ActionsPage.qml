pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// Phone-triggered actions: each one shows up in KDE Connect's "Run command" list on
// the phone and runs the node graph built here.
Item {
    id: page
    property var window

    property string selectedId: ""
    readonly property var current: list.currentItem   // delegate of the selected action
    readonly property var device: App.device
    readonly property bool pluginOn: !!device && device.loadedPlugins.includes("kdeconnect_runcommand")
    property var runLog: []

    function select(id) {
        if (id === selectedId)
            return
        flushSave()
        selectedId = id
        list.currentIndex = App.actions.indexOf(id)
        graph.load(id ? App.actions.graph(id) : ({}))
        canvas.nodeStates = ({})
        canvas.selectedUid = ""
        runLog = id ? App.actions.log(id) : []
        Qt.callLater(canvas.fit)
    }
    function flushSave() {
        if (saveTimer.running) {
            saveTimer.stop()
            App.actions.setGraph(selectedId, graph.toMap())
        }
    }
    function createAction() {
        select(App.actions.create(qsTr("New action")))
        Qt.callLater(() => { nameField.forceActiveFocus(); nameField.selectAll() })
    }
    function deleteAction(id) {
        const name = App.actions.name(id), g = App.actions.graph(id)
        const row = App.actions.indexOf(id)
        App.actions.remove(id)
        const next = App.actions.count > 0 ? App.actions.idAt(Math.min(row, App.actions.count - 1)) : ""
        selectedId = ""
        select(next || "")
        page.window.toast(qsTr("Deleted “%1”").arg(name), qsTr("Undo"), () => {
            const restored = App.actions.create(name)
            App.actions.setGraph(restored, g)
            page.select(restored)
        })
    }
    function run() {
        flushSave()
        App.actions.run(selectedId, device ? device.id : "")
    }
    function addNodeAt(typeId, worldPos) {
        const uid = graph.addNode(typeId, Math.round(worldPos.x / 8) * 8, Math.round(worldPos.y / 8) * 8)
        if (uid)
            canvas.selectedUid = uid
    }

    Component.onCompleted: if (App.actions.count > 0) select(App.actions.idAt(0))

    ActionGraph { id: graph; onEdited: saveTimer.restart() }
    Timer { id: saveTimer; interval: 400; onTriggered: App.actions.setGraph(page.selectedId, graph.toMap()) }
    Timer { id: clearStates; interval: 4000; onTriggered: {
        // Keep failures marked until the next run; let the rest fade.
        const s = {}
        for (const k in canvas.nodeStates) if (canvas.nodeStates[k] === "failed") s[k] = "failed"
        canvas.nodeStates = s
    } }

    Connections {
        target: App.actions
        function onRunStarted(id) {
            if (id !== page.selectedId) return
            clearStates.stop()
            canvas.nodeStates = ({})
            page.runLog = []
        }
        function onNodeState(id, uid, state) {
            if (id !== page.selectedId) return
            const s = Object.assign({}, canvas.nodeStates)
            s[uid] = state
            canvas.nodeStates = s
        }
        function onLogged(id, line) {
            if (id === page.selectedId) page.runLog = page.runLog.concat([line])
        }
        function onRunFinished(id, ok, error) {
            if (id === page.selectedId) clearStates.restart()
            if (!ok && page.window && error !== qsTr("Cancelled"))
                page.window.toast(qsTr("%1 failed: %2").arg(App.actions.name(id)).arg(error))
        }
    }

    Row {
        anchors.fill: parent

        // --- action list -------------------------------------------------------------
        Item {
            id: sidebar
            width: Math.min(320, Math.max(240, page.width * 0.26))
            height: parent.height

            Text {
                id: heading
                x: 24; y: 24
                text: qsTr("Actions")
                color: Theme.surfaceFg
                font.pixelSize: Theme.displaySmall
            }
            Text {
                id: blurb
                anchors.top: heading.bottom
                anchors.topMargin: 8
                x: 24
                width: parent.width - 48
                text: qsTr("Each action appears in KDE Connect's Run command list on your phone. Tap it there to run the graph.")
                color: Theme.surfaceVariantFg
                font.pixelSize: Theme.bodyMedium
                wrapMode: Text.Wrap
            }
            Button3 {
                id: newButton
                anchors.top: blurb.bottom
                anchors.topMargin: 16
                x: 24
                style: "tonal"
                iconName: "add"
                text: qsTr("New action")
                onClicked: page.createAction()
            }
            Button3 {
                id: templateButton
                anchors.top: newButton.bottom
                anchors.topMargin: 4
                x: 12
                style: "text"
                iconName: "headphones"
                text: qsTr("Earbuds swap")
                onClicked: page.select(App.actions.createFromTemplate("swap-earbuds"))
            }

            ListView {
                id: list
                anchors.top: templateButton.bottom
                anchors.topMargin: 16
                anchors.bottom: statusCard.top
                anchors.bottomMargin: 12
                width: parent.width
                clip: true
                spacing: 4
                model: App.actions
                highlightFollowsCurrentItem: false
                B.ScrollBar.vertical: B.ScrollBar {}

                delegate: B.ItemDelegate {
                    id: entry
                    required property int index
                    required property string actionId
                    required property string name
                    required property bool onPhone
                    required property int nodeCount
                    required property bool running
                    required property double lastRun
                    required property bool lastOk
                    required property string lastError
                    readonly property bool selected: actionId === page.selectedId

                    x: 12
                    width: list.width - 24
                    height: 64
                    hoverEnabled: true
                    onClicked: page.select(actionId)

                    background: Rectangle {
                        radius: Theme.radiusL
                        color: entry.selected ? Theme.secondaryContainer : "transparent"
                        StateLayer { hovered: entry.hovered; pressed: entry.pressed; tint: entry.selected ? Theme.secondaryContainerFg : Theme.surfaceFg }
                    }
                    contentItem: Item {
                        Rectangle {
                            id: dot
                            x: 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: 36; height: 36; radius: 18
                            color: entry.selected ? Theme.primary : Theme.surfaceContainerHighest
                            Icon {
                                anchors.centerIn: parent
                                name: entry.running ? "sync" : "bolt"
                                size: 20
                                filled: true
                                color: entry.selected ? Theme.primaryFg : entry.onPhone ? Theme.primary : Theme.outline
                                RotationAnimation on rotation { running: entry.running; from: 360; to: 0; duration: 1200; loops: Animation.Infinite
                                    onRunningChanged: if (!running) parent.rotation = 0 }
                            }
                        }
                        Column {
                            anchors.left: dot.right
                            anchors.leftMargin: 12
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                width: parent.width
                                text: entry.name
                                color: entry.selected ? Theme.secondaryContainerFg : Theme.surfaceFg
                                font.pixelSize: Theme.bodyLarge
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: {
                                    const parts = [entry.onPhone ? qsTr("On phone") : qsTr("Hidden from phone")]
                                    if (entry.running) parts.push(qsTr("running…"))
                                    else if (entry.lastRun > 0) parts.push((entry.lastOk ? qsTr("ran %1") : qsTr("failed %1")).arg(Theme.shortStamp(entry.lastRun)))
                                    return parts.join(" · ")
                                }
                                color: !entry.running && entry.lastRun > 0 && !entry.lastOk ? Theme.error : Theme.surfaceVariantFg
                                font.pixelSize: Theme.labelMedium
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            // Where the actions show up, and why they might not.
            Card {
                id: statusCard
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 16
                x: 12
                width: parent.width - 24
                style: "outlined"
                padding: 12
                implicitHeight: statusCol.implicitHeight + 24
                Column {
                    id: statusCol
                    width: parent.width
                    spacing: 4
                    Row {
                        spacing: 8
                        Icon {
                            name: !App.actions.registering || !page.pluginOn ? "error" : "check"
                            size: 18
                            color: !App.actions.registering || !page.pluginOn ? Theme.error : Theme.primary
                        }
                        Text {
                            text: !App.actions.registering ? qsTr("Demo mode") : !page.device ? qsTr("No phone") : !page.pluginOn ? qsTr("Run commands is off") : qsTr("Registered")
                            color: Theme.surfaceFg
                            font.pixelSize: Theme.labelLarge
                            font.weight: Font.Medium
                        }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        color: Theme.surfaceVariantFg
                        font.pixelSize: Theme.labelMedium
                        text: !App.actions.registering ? qsTr("Actions aren't sent to KDE Connect in demo mode.")
                            : !page.device ? qsTr("Pair a phone to make actions available on it.")
                            : !page.pluginOn ? qsTr("Enable the Run command plugin for this computer in KDE Connect on %1 and on this computer.").arg(page.device.name)
                            : qsTr("On %1: KDE Connect → this computer → Run command.").arg(page.device.name)
                    }
                }
            }
        }

        Rectangle { width: 1; height: parent.height; color: Theme.outlineVariant }

        // --- editor ------------------------------------------------------------------
        Item {
            width: parent.width - sidebar.width - 1
            height: parent.height

            EmptyState {
                anchors.centerIn: parent
                visible: !page.selectedId
                iconName: "account_tree"
                title: qsTr("Build an action")
                text: qsTr("Chain phone, desktop and logic nodes into something you can run from your phone with one tap.")
                Button3 { iconName: "add"; text: qsTr("New action"); onClicked: page.createAction() }
                Button3 { style: "text"; iconName: "headphones"; text: qsTr("Earbuds swap"); onClicked: page.select(App.actions.createFromTemplate("swap-earbuds")) }
            }

            Item {
                anchors.fill: parent
                visible: !!page.selectedId

                // Toolbar
                Item {
                    id: toolbar
                    width: parent.width
                    height: 72

                    TextInput {
                        id: nameField
                        anchors.left: parent.left
                        anchors.leftMargin: 24
                        anchors.right: tools.left
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.surfaceFg
                        font.pixelSize: Theme.headlineSmall
                        selectByMouse: true
                        selectionColor: Theme.alpha(Theme.primary, 0.35)
                        clip: true
                        Binding on text {
                            when: !nameField.activeFocus
                            value: page.current ? page.current.name : ""
                            restoreMode: Binding.RestoreNone
                        }
                        onEditingFinished: { App.actions.rename(page.selectedId, text); focus = false }
                        Keys.onEscapePressed: focus = false
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.bottom
                            height: nameField.activeFocus ? 2 : 1
                            color: nameField.activeFocus ? Theme.primary : "transparent"
                        }
                    }

                    Row {
                        id: tools
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        Chip {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: !!page.current && page.current.onPhone
                            iconName: "smartphone"
                            text: qsTr("Show on phone")
                            onClicked: App.actions.setEnabled(page.selectedId, !page.current.onPhone)
                        }
                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "content_copy"
                            tooltip: qsTr("Duplicate action")
                            onClicked: { page.flushSave(); page.select(App.actions.duplicate(page.selectedId)) }
                        }
                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "delete"
                            tooltip: qsTr("Delete action")
                            onClicked: page.deleteAction(page.selectedId)
                        }
                        Button3 {
                            anchors.verticalCenter: parent.verticalCenter
                            style: "tonal"
                            iconName: "add"
                            text: qsTr("Add node")
                            onClicked: {
                                palette.target = canvas.viewCenterWorld()
                                palette.x = (palette.parent.width - palette.width) / 2
                                palette.y = 8
                                palette.open()
                            }
                        }
                        Button3 {
                            anchors.verticalCenter: parent.verticalCenter
                            readonly property bool running: !!page.current && page.current.running
                            style: "filled"
                            iconName: running ? "stop" : "play_arrow"
                            text: running ? qsTr("Stop") : qsTr("Run")
                            onClicked: running ? App.actions.cancel(page.selectedId) : page.run()
                            B.ToolTip.visible: hovered && !running
                            B.ToolTip.delay: 600
                            B.ToolTip.text: page.device ? qsTr("Run now as if %1 triggered it").arg(page.device.name) : qsTr("Run now (phone nodes need a phone)")
                        }
                    }
                }

                // Canvas
                Rectangle {
                    id: canvasFrame
                    anchors.top: toolbar.bottom
                    anchors.bottom: logPanel.top
                    anchors.bottomMargin: 12
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    radius: Theme.radiusL
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.outlineVariant
                    clip: true

                    NodeCanvas {
                        id: canvas
                        anchors.fill: parent
                        anchors.margins: 1
                        graph: graph
                        onPaletteRequested: (worldPos, viewPos) => {
                            palette.target = worldPos
                            const p = canvas.mapToItem(palette.parent, viewPos.x, viewPos.y)
                            palette.x = Math.min(p.x, palette.parent.width - palette.width - 8)
                            palette.y = Math.max(8, Math.min(p.y, palette.parent.height - palette.height - 8))
                            palette.open()
                        }
                    }

                    // Zoom controls
                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 12
                        width: zoomRow.width + 8
                        height: 48
                        radius: 24
                        color: Theme.surfaceContainerHigh
                        Row {
                            id: zoomRow
                            anchors.centerIn: parent
                            IconButton { iconName: "zoom_out"; tooltip: qsTr("Zoom out"); onClicked: canvas.zoomAt(Qt.point(canvas.width / 2, canvas.height / 2), 1 / 1.2) }
                            Text {
                                width: 44
                                anchors.verticalCenter: parent.verticalCenter
                                horizontalAlignment: Text.AlignHCenter
                                text: Math.round(canvas.zoom * 100) + "%"
                                color: Theme.surfaceVariantFg
                                font.pixelSize: Theme.labelMedium
                            }
                            IconButton { iconName: "zoom_in"; tooltip: qsTr("Zoom in"); onClicked: canvas.zoomAt(Qt.point(canvas.width / 2, canvas.height / 2), 1.2) }
                            IconButton { iconName: "fit_screen"; tooltip: qsTr("Fit to view"); onClicked: canvas.fit() }
                        }
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 14
                        text: qsTr("Drag from a pin to connect · right-click a pin to disconnect · double-click empty space to add")
                        color: Theme.alpha(Theme.surfaceVariantFg, 0.8)
                        font.pixelSize: Theme.labelSmall
                    }
                }

                // Run log
                Card {
                    id: logPanel
                    property bool expanded: false
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 16
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    style: "filled"
                    color: Theme.surfaceContainer
                    padding: 0
                    height: expanded ? 200 : 44
                    Behavior on height { NumberAnimation { duration: Theme.durationMedium; easing.type: Easing.OutCubic } }
                    clip: true

                    Item {
                        id: logHeader
                        width: parent.width
                        height: 44
                        Icon {
                            id: logIcon
                            x: 14
                            anchors.verticalCenter: parent.verticalCenter
                            size: 18
                            name: "subject"
                        }
                        Text {
                            anchors.left: logIcon.right
                            anchors.leftMargin: 10
                            anchors.right: parent.right
                            anchors.rightMargin: 40
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.runLog.length ? qsTr("Run log") + "  ·  " + page.runLog[page.runLog.length - 1] : qsTr("Run log  ·  not run yet")
                            color: Theme.surfaceVariantFg
                            font.pixelSize: Theme.labelMedium
                            font.family: "monospace"
                            elide: Text.ElideRight
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            text: logPanel.expanded ? "▾" : "▴"
                            color: Theme.surfaceVariantFg
                        }
                        TapHandler { onTapped: logPanel.expanded = !logPanel.expanded }
                    }
                    ListView {
                        id: logList
                        anchors.top: logHeader.bottom
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 8
                        width: parent.width
                        clip: true
                        model: page.runLog
                        onCountChanged: positionViewAtEnd()
                        B.ScrollBar.vertical: B.ScrollBar {}
                        delegate: Text {
                            required property string modelData
                            x: 16
                            width: logList.width - 32
                            text: modelData
                            color: modelData.indexOf("✗") >= 0 ? Theme.error : Theme.surfaceFg
                            font.pixelSize: Theme.labelMedium
                            font.family: "monospace"
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                        }
                    }
                }

                NodePalette {
                    id: palette
                    property point target
                    colorsFor: canvas.categoryColors
                    onChosen: typeId => page.addNodeAt(typeId, target)
                }
            }
        }
    }
}
