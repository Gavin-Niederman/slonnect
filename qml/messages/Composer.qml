pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// Message input with attachments. Enter sends, Shift+Enter inserts a newline.
Rectangle {
    id: root
    signal attachContact()
    signal sent()

    property var pending: []   // local file paths

    function addAttachment(pathOrUrl) {
        const path = pathOrUrl.startsWith("file://") ? decodeURIComponent(pathOrUrl.slice(7)) : pathOrUrl
        if (!path || pending.indexOf(path) >= 0)
            return
        pending = pending.concat([path])
        input.forceActiveFocus()
    }
    function send() {
        if (App.sms.send(input.text, pending)) {
            input.text = ""
            pending = []
            sent()
        }
    }

    // Drafts survive switching threads.
    property var drafts: ({})
    property string draftKey: App.sms.currentThread >= 0 ? "t" + App.sms.currentThread : "n" + App.sms.currentAddresses.join(",")
    onDraftKeyChanged: {
        input.text = drafts[draftKey] || ""
        pending = []
        if (visible)
            input.forceActiveFocus()
    }

    implicitHeight: column.implicitHeight + 24
    color: Theme.surface

    Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        spacing: 8

        // Pending attachments
        Flow {
            width: parent.width
            visible: root.pending.length > 0
            spacing: 8
            leftPadding: 52
            Repeater {
                model: root.pending
                delegate: Chip {
                    required property string modelData
                    required property int index
                    iconName: modelData.toLowerCase().endsWith(".vcf") ? "contact_page" : "attach_file"
                    text: modelData.split("/").pop()
                    removable: true
                    onRemoveClicked: {
                        const copy = root.pending.slice()
                        copy.splice(index, 1)
                        root.pending = copy
                    }
                }
            }
        }

        Item {
            width: parent.width
            height: Math.max(48, field.height)

            IconButton {
                id: attach
                iconName: "add_circle"
                tooltip: qsTr("Attach")
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 4
                onClicked: attachMenu.popup(attach, 0, -attachMenu.implicitHeight)

                B.Menu {
                    id: attachMenu
                    B.MenuItem {
                        text: qsTr("Photo or file…")
                        onTriggered: fileDialog.open()
                    }
                    B.MenuItem {
                        text: qsTr("Contact card…")
                        onTriggered: root.attachContact()
                    }
                }
            }

            Rectangle {
                id: field
                anchors.left: attach.right
                anchors.leftMargin: 8
                anchors.right: sendButton.left
                anchors.rightMargin: 8
                anchors.bottom: parent.bottom
                height: Math.min(160, Math.max(48, input.implicitHeight + 4))
                radius: 24
                color: Theme.surfaceContainerHigh

                B.ScrollView {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    B.TextArea {
                        id: input
                        placeholderText: App.sms.available ? qsTr("Text message") : qsTr("Phone not connected")
                        placeholderTextColor: Theme.surfaceVariantFg
                        color: Theme.surfaceFg
                        selectionColor: Theme.alpha(Theme.primary, 0.35)
                        font.pixelSize: Theme.bodyLarge
                        wrapMode: TextEdit.Wrap
                        background: null
                        topPadding: 13
                        bottomPadding: 13
                        onTextChanged: root.drafts[root.draftKey] = text
                        function handleEnter(event) {
                            if (event.modifiers & Qt.ShiftModifier)
                                event.accepted = false
                            else
                                root.send()
                        }
                        Keys.onReturnPressed: (event) => handleEnter(event)
                        Keys.onEnterPressed: (event) => handleEnter(event)
                    }
                }
            }

            IconButton {
                id: sendButton
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: 48; height: 48
                style: "filled"
                iconName: "send"
                tooltip: qsTr("Send (Enter)")
                enabled: input.text.trim().length > 0 || root.pending.length > 0
                onClicked: root.send()
            }
        }

        Text {
            visible: input.text.length > 140
            anchors.right: parent.right
            anchors.rightMargin: 64
            // GSM-7 segments are 160 chars (153 when concatenated); good enough as a hint.
            text: input.text.length + (input.text.length > 160 ? " · " + Math.ceil(input.text.length / 153) + " SMS" : "")
            color: Theme.surfaceVariantFg
            font.pixelSize: Theme.labelSmall
        }
    }

    FilePicker {
        id: fileDialog
        title: qsTr("Attach files")
        onPicked: (urls) => { for (const u of urls) root.addAttachment(u) }
    }
}
