pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// Inline editor for an unconnected node input: text, number, bool or choice. Text
// pins with a `completion` kind suggest values (D-Bus names, Bluetooth devices, …)
// looked up live by App.completer; ↑/↓ pick, Enter or Tab takes one.
Item {
    id: root
    required property var pin
    property var value
    // The node's current values, so suggestions can depend on other inputs.
    property var context: ({})
    signal edited(var value)
    // Other inputs a chosen suggestion wants filled in, e.g. { args: "[\"\", 0]" }.
    signal fill(var values)

    implicitHeight: 24

    readonly property bool completes: !!root.pin.completion
    property var suggestions: []
    property int requestId: -1
    property bool typed: false
    readonly property var shown: {
        const q = input.text.trim().toLowerCase()
        if (!typed || !q)
            return suggestions.slice(0, 200)
        const hits = suggestions.filter(s => s.value.toLowerCase().includes(q) || (s.detail || "").toLowerCase().includes(q))
        // Prefix matches first.
        hits.sort((a, b) => (b.value.toLowerCase().startsWith(q) ? 1 : 0) - (a.value.toLowerCase().startsWith(q) ? 1 : 0))
        return hits.slice(0, 200)
    }
    onShownChanged: {
        list.currentIndex = shown.length ? 0 : -1
        if (!shown.length)
            suggest.close()
    }

    function request() {
        loading = true
        requestId = App.completer.complete(root.pin.completion, root.context || ({}))
    }
    property bool loading: false
    function choose(s) {
        typed = false
        input.text = s.value
        root.edited(s.value)
        if (s.fill)
            root.fill(s.fill)
        suggest.close()
    }

    Connections {
        target: root.completes ? App.completer : null
        function onCompleted(id, items) {
            if (id !== root.requestId)
                return
            root.loading = false
            root.suggestions = items
            if (input.activeFocus && root.shown.length)
                suggest.open()
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.pin.kind === "text" || root.pin.kind === "number"
        radius: 6
        color: Theme.surfaceContainerHighest
        border.width: input.activeFocus ? 1 : 0
        border.color: Theme.primary

        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: root.completes ? 20 : 8
            verticalAlignment: TextInput.AlignVCenter
            clip: true
            color: Theme.surfaceFg
            selectionColor: Theme.alpha(Theme.primary, 0.35)
            selectByMouse: true
            font.pixelSize: Theme.labelMedium
            font.family: root.pin.kind === "number" ? "monospace" : Qt.application.font.family
            inputMethodHints: root.pin.kind === "number" ? Qt.ImhFormattedNumbersOnly : Qt.ImhNone
            validator: root.pin.kind === "number" ? numberValidator : null
            // Keep the typed text while editing; resync from the model otherwise.
            Binding on text {
                when: !input.activeFocus
                value: root.value === undefined || root.value === null ? "" : String(root.value)
                restoreMode: Binding.RestoreNone
            }
            onEditingFinished: root.edited(root.pin.kind === "number" ? Number(text || 0) : text)
            // Show the start of long values when not editing.
            onTextChanged: if (!activeFocus) cursorPosition = 0
            onActiveFocusChanged: {
                if (!activeFocus) {
                    cursorPosition = 0
                    suggest.close()
                } else if (root.completes) {
                    root.typed = false
                    root.request()
                }
            }
            onTextEdited: {
                if (!root.completes)
                    return
                root.typed = true
                if (root.shown.length)
                    suggest.open()
            }
            Keys.onPressed: event => {
                if (!suggest.opened)
                    return
                if (event.key === Qt.Key_Down) {
                    list.incrementCurrentIndex()
                    event.accepted = true
                } else if (event.key === Qt.Key_Up) {
                    list.decrementCurrentIndex()
                    event.accepted = true
                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Tab) && list.currentIndex >= 0) {
                    root.choose(root.shown[list.currentIndex])
                    event.accepted = true
                }
            }
            Keys.onEscapePressed: {
                if (suggest.opened)
                    suggest.close()
                else
                    focus = false
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: !input.text && !input.activeFocus
                text: root.pin.placeholder || ""
                color: Theme.alpha(Theme.surfaceVariantFg, 0.6)
                font: input.font
                elide: Text.ElideRight
                width: parent.width
            }
        }
        // Opens the suggestion list without typing.
        Icon {
            visible: root.completes
            anchors.right: parent.right
            anchors.rightMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            name: "expand_more"
            size: 16
            color: Theme.surfaceVariantFg
            opacity: root.loading ? 0.4 : 1
            TapHandler {
                onTapped: {
                    input.forceActiveFocus()
                    root.typed = false
                    if (root.shown.length)
                        suggest.open()
                }
            }
        }
        DoubleValidator { id: numberValidator; notation: DoubleValidator.StandardNotation; locale: "C" }

        B.Popup {
            id: suggest
            y: parent.height + 2
            width: Math.max(320, parent.width)
            height: Math.min(list.contentHeight + 8, 280)
            padding: 4
            focus: false
            closePolicy: B.Popup.CloseOnEscape | B.Popup.CloseOnPressOutsideParent
            background: Rectangle {
                radius: Theme.radiusM
                color: Theme.surfaceContainerHigh
                border.width: 1
                border.color: Theme.outlineVariant
            }
            contentItem: ListView {
                id: list
                clip: true
                model: root.shown
                highlightMoveDuration: 0
                boundsBehavior: Flickable.StopAtBounds
                B.ScrollBar.vertical: B.ScrollBar {}
                delegate: Rectangle {
                    id: entry
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: entry.modelData.detail ? 40 : 28
                    radius: 6
                    color: ListView.isCurrentItem ? Theme.secondaryContainer : hover.hovered ? Theme.alpha(Theme.surfaceFg, 0.08) : "transparent"
                    Column {
                        x: 8
                        width: parent.width - 16
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            width: parent.width
                            text: entry.modelData.value
                            color: entry.ListView.isCurrentItem ? Theme.secondaryContainerFg : Theme.surfaceFg
                            font.pixelSize: Theme.labelLarge
                            elide: Text.ElideMiddle
                            textFormat: Text.PlainText
                        }
                        Text {
                            width: parent.width
                            visible: text.length > 0
                            text: entry.modelData.detail || ""
                            color: Theme.surfaceVariantFg
                            font.pixelSize: Theme.labelSmall
                            font.family: "monospace"
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                    }
                    HoverHandler { id: hover }
                    TapHandler { onTapped: root.choose(entry.modelData) }
                }
            }
        }
    }

    // Bool: a small switch.
    Rectangle {
        visible: root.pin.kind === "bool"
        anchors.verticalCenter: parent.verticalCenter
        width: 36; height: 20; radius: 10
        color: root.value ? Theme.primary : Theme.surfaceContainerHighest
        border.width: root.value ? 0 : 1
        border.color: Theme.outline
        Rectangle {
            width: 14; height: 14; radius: 7
            anchors.verticalCenter: parent.verticalCenter
            x: root.value ? parent.width - width - 3 : 3
            color: root.value ? Theme.primaryFg : Theme.outline
            Behavior on x { NumberAnimation { duration: Theme.durationShort } }
        }
        TapHandler { onTapped: root.edited(!root.value) }
    }

    // Choice: a compact drop-down.
    Rectangle {
        id: choiceBox
        visible: root.pin.kind === "choice"
        anchors.fill: parent
        radius: 6
        color: Theme.surfaceContainerHighest
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.right: arrow.left
            anchors.verticalCenter: parent.verticalCenter
            text: root.value === undefined ? "" : String(root.value)
            color: Theme.surfaceFg
            font.pixelSize: Theme.labelMedium
            elide: Text.ElideRight
        }
        Text {
            id: arrow
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: "▾"
            color: Theme.surfaceVariantFg
            font.pixelSize: Theme.labelMedium
        }
        TapHandler { onTapped: menu.popup(choiceBox, 0, choiceBox.height) }
        B.Menu {
            id: menu
            Repeater {
                model: root.pin.kind === "choice" ? root.pin.options : []
                delegate: B.MenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: root.edited(modelData)
                }
            }
        }
    }
}
