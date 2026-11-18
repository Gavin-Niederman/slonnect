pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// Inline editor for an unconnected node input: text, number, bool or choice.
Item {
    id: root
    required property var pin
    property var value
    signal edited(var value)

    implicitHeight: 24

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
            anchors.rightMargin: 8
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
            onActiveFocusChanged: if (!activeFocus) cursorPosition = 0
            Keys.onEscapePressed: focus = false

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
        DoubleValidator { id: numberValidator; notation: DoubleValidator.StandardNotation; locale: "C" }
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
