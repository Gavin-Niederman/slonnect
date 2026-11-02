pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// Modal contact chooser. "recipients" mode collects one or more addresses
// (contacts or typed numbers); "card" mode picks a contact to share as a vCard.
B.Popup {
    id: root
    signal recipientsChosen(var addresses)
    signal cardChosen(string path)

    property string mode: "recipients"
    property var chosen: []   // [{ name, address }]

    function openForRecipients() { mode = "recipients"; chosen = []; open() }
    function openForCard() { mode = "card"; chosen = []; open() }

    function add(name, address) {
        if (!address || chosen.some(c => c.address === address))
            return
        chosen = chosen.concat([{ name: name, address: address }])
        search.text = ""
    }
    function looksLikeAddress(t) { return /^[+\d][\d\s().-]{2,}$/.test(t.trim()) || /^\S+@\S+$/.test(t.trim()) }
    function finish() {
        if (mode !== "recipients")
            return
        if (looksLikeAddress(search.text))
            add(search.text.trim(), search.text.trim())
        if (chosen.length === 0)
            return
        recipientsChosen(chosen.map(c => c.address))
        close()
    }

    anchors.centerIn: parent
    width: Math.min(460, parent ? parent.width - 48 : 460)
    height: Math.min(640, parent ? parent.height - 48 : 640)
    modal: true
    focus: true
    padding: 0
    onOpened: { search.text = ""; search.field.forceActiveFocus() }
    onClosed: App.contacts.setFilter("")

    B.Overlay.modal: Rectangle { color: Theme.alpha(Theme.scrim, 0.32) }
    background: Rectangle { radius: Theme.radiusXL; color: Theme.surfaceContainerHigh }

    contentItem: Item {
        Text {
            id: heading
            x: 24; y: 24
            text: root.mode === "card" ? qsTr("Share a contact") : qsTr("New conversation")
            color: Theme.surfaceFg
            font.pixelSize: Theme.headlineSmall
        }

        Flow {
            id: chips
            anchors.top: heading.bottom
            anchors.topMargin: 16
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 8
            visible: root.chosen.length > 0
            Repeater {
                model: root.chosen
                delegate: Chip {
                    required property var modelData
                    required property int index
                    checked: true
                    checkable: false
                    iconName: "person"
                    text: modelData.name
                    removable: true
                    onRemoveClicked: { const c = root.chosen.slice(); c.splice(index, 1); root.chosen = c }
                }
            }
        }

        SearchBar {
            id: search
            anchors.top: chips.visible ? chips.bottom : heading.bottom
            anchors.topMargin: 16
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            color: Theme.surfaceContainerHighest
            placeholder: root.mode === "card" ? qsTr("Search contacts") : qsTr("Name, phone number or email")
            onTextChanged: App.contacts.setFilter(text)
            onAccepted: {
                if (root.mode === "recipients" && root.looksLikeAddress(text))
                    root.add(text.trim(), text.trim())
                else if (list.count > 0)
                    list.itemAtIndex(0).clicked()
                else
                    root.finish()
            }
        }

        // Typed number shortcut
        B.ItemDelegate {
            id: typed
            visible: root.mode === "recipients" && root.looksLikeAddress(search.text)
            anchors.top: search.bottom
            anchors.topMargin: 8
            width: parent.width
            height: visible ? 56 : 0
            onClicked: root.add(search.text.trim(), search.text.trim())
            background: StateLayer { hovered: typed.hovered; pressed: typed.pressed; radius: 0 }
            contentItem: Row {
                leftPadding: 24
                spacing: 16
                Icon { name: "dialpad"; anchors.verticalCenter: parent.verticalCenter; color: Theme.primary }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Send to %1").arg(search.text.trim())
                    color: Theme.surfaceFg
                    font.pixelSize: Theme.bodyLarge
                }
            }
        }

        ListView {
            id: list
            anchors.top: typed.bottom
            anchors.topMargin: 8
            anchors.bottom: footer.top
            anchors.left: parent.left
            anchors.right: parent.right
            clip: true
            model: App.contacts.filtered
            reuseItems: true
            B.ScrollBar.vertical: B.ScrollBar {}
            delegate: B.ItemDelegate {
                id: row
                required property string uid
                required property string name
                required property var numbers
                required property string primaryNumber
                required property string avatar
                width: list.width
                height: 64
                hoverEnabled: true
                onClicked: {
                    if (root.mode === "card") {
                        const path = App.contacts.exportVCard(uid)
                        if (path) { root.cardChosen(path); root.close() }
                    } else {
                        root.add(name, primaryNumber)
                    }
                }
                background: StateLayer { hovered: row.hovered; pressed: row.pressed; radius: 0 }
                contentItem: Item {
                    Avatar { id: av; x: 24; anchors.verticalCenter: parent.verticalCenter; name: row.name; source: row.avatar }
                    Column {
                        anchors.left: av.right
                        anchors.leftMargin: 16
                        anchors.right: parent.right
                        anchors.rightMargin: 24
                        anchors.verticalCenter: parent.verticalCenter
                        Text { width: parent.width; text: row.name; elide: Text.ElideRight; color: Theme.surfaceFg; font.pixelSize: Theme.bodyLarge }
                        Text {
                            width: parent.width
                            text: row.numbers.join(" · ")
                            elide: Text.ElideRight
                            color: Theme.surfaceVariantFg
                            font.pixelSize: Theme.bodyMedium
                        }
                    }
                }
            }

            EmptyState {
                anchors.centerIn: parent
                visible: list.count === 0 && !typed.visible
                iconName: App.contacts.count === 0 ? "contacts" : "search_off"
                title: App.contacts.count === 0 ? qsTr("No contacts synced") : qsTr("No matches")
                text: App.contacts.count > 0 ? ""
                      : App.contacts.phoneSilent
                      ? qsTr("Your phone didn't answer. In KDE Connect on the phone, open this computer's plugin settings and turn on \"Contacts synchronizer\", then sync again. You can still type a number.")
                      : qsTr("Allow Contacts access for KDE Connect on your phone and enable its Contacts synchronizer plugin, then sync. You can still type a number.")
                Button3 {
                    visible: App.contacts.count === 0
                    style: "tonal"
                    iconName: "sync"
                    text: App.contacts.syncing ? qsTr("Syncing…") : qsTr("Sync contacts")
                    enabled: !App.contacts.syncing
                    onClicked: App.contacts.sync()
                }
            }
        }

        Row {
            id: footer
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 16
            spacing: 8
            Button3 { style: "text"; text: qsTr("Cancel"); onClicked: root.close() }
            Button3 {
                visible: root.mode === "recipients"
                style: "filled"
                text: qsTr("Start")
                enabled: root.chosen.length > 0 || root.looksLikeAddress(search.text)
                onClicked: root.finish()
            }
        }
    }
}
