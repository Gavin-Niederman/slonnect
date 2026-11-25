pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// What KDE Connect's own app and settings module offer: this computer's name and
// transports, devices reached by address, and the selected device's plugins with
// their options.
Item {
    id: page
    property var window
    readonly property var device: App.device
    readonly property string deviceName: device ? device.name : qsTr("the device")

    // Bumped whenever plugin settings change on disk, so bindings re-read them.
    property int revision: 0
    Connections {
        target: App.plugins
        function onValuesChanged() { page.revision++ }
        function onDeviceChanged() { page.revision++ }
    }
    function setting(plugin, key, fallback) { page.revision; return App.plugins.value(plugin, key, fallback) }

    // Plugins with options, and the form for each.
    readonly property var configurable: ({
        "kdeconnect_clipboard": clipboardForm,
        "kdeconnect_share": shareForm,
        "kdeconnect_findthisdevice": findForm,
        "kdeconnect_pausemusic": pauseForm,
        "kdeconnect_presenter": presenterForm,
        "kdeconnect_sendnotifications": sendNotificationsForm,
        "kdeconnect_shareinputdevices": shareInputForm,
        "kdeconnect_runcommand": runCommandForm
    })
    property string expanded: page.window ? page.window.focusPlugin : ""
    property string filter

    // --- small building blocks ------------------------------------------------
    component OptionRow: Item {
        id: optRow
        property string title
        property string subtitle
        default property alias trailing: trail.data
        width: parent ? parent.width : 0
        height: Math.max(56, texts.implicitHeight + 16)
        Column {
            id: texts
            anchors.left: parent.left
            anchors.right: trail.left
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text { width: parent.width; text: optRow.title; color: Theme.surfaceFg; font.pixelSize: Theme.bodyLarge; wrapMode: Text.Wrap }
            Text { width: parent.width; visible: text.length > 0; text: optRow.subtitle; color: Theme.surfaceVariantFg; font.pixelSize: Theme.bodyMedium; wrapMode: Text.Wrap }
        }
        Row {
            id: trail
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
        }
    }
    component BoolOption: OptionRow {
        id: boolOpt
        property string plugin
        property string key
        property bool fallback
        Switch3 {
            checked: page.setting(boolOpt.plugin, boolOpt.key, boolOpt.fallback)
            onToggled: App.plugins.setValue(boolOpt.plugin, boolOpt.key, checked)
        }
    }
    component ChoiceOption: OptionRow {
        id: choiceOpt
        property string plugin
        property string key
        property var fallback
        property var choices: [] // [{ value, label }]
        Repeater {
            model: choiceOpt.choices
            delegate: Chip {
                required property var modelData
                checkable: true
                checked: page.setting(choiceOpt.plugin, choiceOpt.key, choiceOpt.fallback) === modelData.value
                text: modelData.label
                onClicked: App.plugins.setValue(choiceOpt.plugin, choiceOpt.key, modelData.value)
            }
        }
    }
    component Field: Rectangle {
        property alias text: input.text
        property alias placeholder: input.placeholderText
        property alias input: input
        signal accepted()
        height: 44
        radius: 22
        color: Theme.surfaceContainerHigh
        B.TextField {
            id: input
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            background: null
            color: Theme.surfaceFg
            placeholderTextColor: Theme.surfaceVariantFg
            selectionColor: Theme.alpha(Theme.primary, 0.35)
            font.pixelSize: Theme.bodyMedium
            onAccepted: parent.accepted()
        }
    }

    // --- per-plugin forms -----------------------------------------------------
    Component {
        id: clipboardForm
        Column {
            BoolOption { plugin: "kdeconnect_clipboard"; key: "autoShare"; fallback: page.setting("kdeconnect_clipboard", "sendUnknown", true)
                         title: qsTr("Share this computer's clipboard automatically"); subtitle: qsTr("Otherwise only when you send it") }
            BoolOption { plugin: "kdeconnect_clipboard"; key: "sendPassword"; fallback: true
                         title: qsTr("Include passwords"); subtitle: qsTr("Text that password managers mark as secret") }
            OptionRow {
                title: qsTr("Largest file to share through the clipboard")
                Field {
                    width: 96
                    text: page.setting("kdeconnect_clipboard", "maxClipboardFileSizeMB", 50)
                    input.validator: IntValidator { bottom: 0; top: 4096 }
                    input.horizontalAlignment: Text.AlignRight
                    onAccepted: App.plugins.setValue("kdeconnect_clipboard", "maxClipboardFileSizeMB", parseInt(text) || 0)
                    input.onEditingFinished: accepted()
                }
                Text { text: qsTr("MB"); color: Theme.surfaceVariantFg; font.pixelSize: Theme.bodyMedium; anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }
    Component {
        id: shareForm
        Column {
            OptionRow {
                title: qsTr("Save received files in")
                subtitle: App.share.incomingFolder + "\n" + qsTr("\"%1\" in a folder name stands for the device's name.")
                Button3 {
                    style: "tonal"; iconName: "folder_open"; text: qsTr("Change…")
                    onClicked: folderPicker.open()
                }
                Button3 {
                    style: "text"; text: qsTr("Default")
                    visible: page.setting("kdeconnect_share", "incoming_path", "") !== ""
                    onClicked: App.plugins.reset("kdeconnect_share", "incoming_path")
                }
                FilePicker {
                    id: folderPicker
                    title: qsTr("Folder for files from %1").arg(page.deviceName)
                    directory: true
                    onPicked: (urls) => App.plugins.setValue("kdeconnect_share", "incoming_path", App.localPath(urls[0]))
                }
            }
        }
    }
    Component {
        id: findForm
        Column {
            OptionRow {
                id: soundRow
                readonly property string sound: page.setting("kdeconnect_findthisdevice", "ringtone", "")
                title: qsTr("Sound to play when %1 looks for this computer").arg(page.deviceName)
                subtitle: sound || qsTr("KDE Connect's default sound")
                Button3 { style: "tonal"; iconName: "music_note"; text: qsTr("Choose…"); onClicked: soundPicker.open() }
                Button3 { style: "text"; text: qsTr("Default"); visible: !!soundRow.sound; onClicked: App.plugins.reset("kdeconnect_findthisdevice", "ringtone") }
                FilePicker {
                    id: soundPicker
                    title: qsTr("Choose a sound")
                    multiple: false
                    onPicked: (urls) => App.plugins.setValue("kdeconnect_findthisdevice", "ringtone", App.localPath(urls[0]))
                }
            }
        }
    }
    Component {
        id: pauseForm
        Column {
            ChoiceOption {
                plugin: "kdeconnect_pausemusic"; key: "conditionTalking"; fallback: false
                title: qsTr("When")
                choices: [{ value: false, label: qsTr("As soon as the phone rings") }, { value: true, label: qsTr("Only while talking") }]
            }
            BoolOption { plugin: "kdeconnect_pausemusic"; key: "actionPause"; fallback: true; title: qsTr("Pause media players") }
            BoolOption { plugin: "kdeconnect_pausemusic"; key: "actionMute"; fallback: false; title: qsTr("Mute system sound") }
            BoolOption { plugin: "kdeconnect_pausemusic"; key: "actionResume"; fallback: true; title: qsTr("Resume when the call ends") }
        }
    }
    Component {
        id: presenterForm
        Column {
            ChoiceOption {
                plugin: "kdeconnect_presenter"; key: "current"; fallback: "PresenterRedDot.qml"
                title: qsTr("Pointer style")
                subtitle: qsTr("How the phone's laser pointer looks on this screen")
                choices: [{ value: "PresenterRedDot.qml", label: qsTr("Red dot") }, { value: "PresenterElectric.qml", label: qsTr("Electric") }]
            }
        }
    }
    Component {
        id: sendNotificationsForm
        Column {
            BoolOption { plugin: "kdeconnect_sendnotifications"; key: "generalPersistent"; fallback: false; title: qsTr("Persistent notifications only") }
            BoolOption { plugin: "kdeconnect_sendnotifications"; key: "generalIncludeBody"; fallback: true; title: qsTr("Include the body") }
            BoolOption { plugin: "kdeconnect_sendnotifications"; key: "generalSynchronizeIcons"; fallback: true; title: qsTr("Include icons") }
            ChoiceOption {
                plugin: "kdeconnect_sendnotifications"; key: "generalUrgency"; fallback: 0
                title: qsTr("Minimum urgency")
                choices: [{ value: 0, label: qsTr("Low") }, { value: 1, label: qsTr("Normal") }, { value: 2, label: qsTr("Critical") }]
            }
        }
    }
    Component {
        id: shareInputForm
        Column {
            ChoiceOption {
                plugin: "kdeconnect_shareinputdevices"; key: "edge"; fallback: Qt.LeftEdge
                title: qsTr("Leave the screen at the")
                subtitle: qsTr("The pointer crosses over to %1 at this edge").arg(page.deviceName)
                choices: [{ value: Qt.TopEdge, label: qsTr("Top") }, { value: Qt.LeftEdge, label: qsTr("Left") },
                          { value: Qt.RightEdge, label: qsTr("Right") }, { value: Qt.BottomEdge, label: qsTr("Bottom") }]
            }
        }
    }
    Component {
        id: runCommandForm
        Column {
            OptionRow {
                title: qsTr("Commands %1 can run here are Slonnect actions").arg(page.deviceName)
                subtitle: qsTr("Commands added in KDE Connect's own settings are kept and still work.")
                Button3 { style: "tonal"; iconName: "bolt"; text: qsTr("Open Actions"); onClicked: page.window.go("actions") }
            }
        }
    }

    // --- layout ---------------------------------------------------------------
    B.ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth

        Column {
            id: content
            width: Math.min(scroll.availableWidth - 48, 880)
            x: (scroll.availableWidth - width) / 2
            topPadding: 24
            bottomPadding: 24
            spacing: 16

            Text { text: qsTr("Settings"); color: Theme.surfaceFg; font.pixelSize: Theme.displaySmall }

            // This computer
            SectionHeader { text: qsTr("This computer") }
            Card {
                width: parent.width
                style: "outlined"
                implicitHeight: hostCol.implicitHeight + 32
                Column {
                    id: hostCol
                    width: parent.width
                    spacing: 4
                    OptionRow {
                        title: qsTr("Name")
                        subtitle: qsTr("What other devices call this computer") + (App.host.id ? " · ID " + App.host.id : "")
                        Field {
                            id: nameField
                            width: 220
                            text: App.host.name
                            onAccepted: App.host.setName(text)
                        }
                        Button3 {
                            style: "tonal"; text: qsTr("Rename")
                            enabled: nameField.text.trim().length > 0 && nameField.text.trim() !== App.host.name
                            onClicked: App.host.setName(nameField.text)
                        }
                    }
                    Repeater {
                        model: App.host.backends
                        delegate: OptionRow {
                            id: backendRow
                            required property var modelData
                            title: modelData.name
                            subtitle: modelData.id === "lan" ? qsTr("Find and connect to devices on the local network")
                                    : modelData.id === "bluetooth" ? qsTr("Connect over Bluetooth (experimental in KDE Connect)") : ""
                            Switch3 {
                                checked: backendRow.modelData.enabled
                                onToggled: App.host.setBackendEnabled(backendRow.modelData.id, checked)
                            }
                        }
                    }
                }
            }

            // Messages backend
            SectionHeader { text: qsTr("Messages") }
            Card {
                id: msgCard
                width: parent.width
                style: "outlined"
                implicitHeight: msgCol.implicitHeight + 32
                readonly property var gm: App.gmessages
                readonly property bool useGm: App.sms.backend === "gmessages"
                readonly property bool signedOut: gm.state === "logged_out" || gm.state === "needs_relogin"
                readonly property string stateText: !gm.helperRunning ? qsTr("Helper not running")
                    : gm.state === "ready" ? qsTr("Connected")
                    : gm.state === "connecting" ? qsTr("Connecting…")
                    : gm.state === "pairing" ? qsTr("Pairing…")
                    : gm.state === "phone_offline" ? qsTr("Phone offline")
                    : gm.state === "inactive" ? qsTr("Open in another browser")
                    : gm.state === "needs_relogin" ? qsTr("Sign in again")
                    : qsTr("Not signed in")
                Column {
                    id: msgCol
                    width: parent.width
                    spacing: 8
                    OptionRow {
                        title: qsTr("Send and receive messages through")
                        subtitle: msgCard.useGm ? qsTr("Google Messages for Web: full RCS (reactions, replies, typing, read receipts) from anywhere. Google Messages on the phone relays everything, so the phone must stay online. KDE Connect still handles notifications, files and the rest.")
                                                : qsTr("KDE Connect's SMS plugin, over your local network or VPN.")
                        Chip { checkable: true; checked: !msgCard.useGm; text: qsTr("KDE Connect"); onClicked: App.sms.backend = "kdeconnect" }
                        Chip { checkable: true; checked: msgCard.useGm; text: qsTr("Google Messages"); onClicked: App.sms.backend = "gmessages" }
                    }

                    // Status
                    OptionRow {
                        visible: msgCard.useGm
                        title: msgCard.stateText + (msgCard.gm.account ? " · " + msgCard.gm.account : "")
                        subtitle: msgCard.gm.error || (msgCard.gm.secrets === "file" ? qsTr("No Secret Service is running, so the session is kept in a file only you can read.") : "")
                        Button3 {
                            style: "filled"; iconName: "devices"; text: qsTr("Use here")
                            visible: msgCard.gm.state === "inactive"
                            onClicked: msgCard.gm.takeOver()
                        }
                        Button3 {
                            style: "tonal"; iconName: "refresh"; text: qsTr("Start helper")
                            visible: !msgCard.gm.helperRunning
                            onClicked: msgCard.gm.ensureRunning()
                        }
                        Button3 {
                            style: "text"; iconName: "logout"; text: qsTr("Sign out")
                            visible: msgCard.gm.helperRunning && !msgCard.signedOut && msgCard.gm.state !== "pairing"
                            onClicked: msgCard.gm.logout()
                        }
                    }

                    // Pairing: the emoji to tap on the phone.
                    Column {
                        visible: msgCard.useGm && msgCard.gm.state === "pairing"
                        width: parent.width
                        spacing: 8
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: msgCard.gm.emoji.length > 0
                            text: msgCard.gm.emoji
                            font.pixelSize: 72
                        }
                        B.BusyIndicator {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: msgCard.gm.emoji.length === 0
                            running: visible
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: msgCard.gm.emoji ? qsTr("Google Messages on your phone now shows a few emoji. Tap the one shown here.")
                                                   : qsTr("Contacting Google and your phone…")
                            color: Theme.surfaceFg
                            font.pixelSize: Theme.bodyLarge
                        }
                        Button3 { anchors.horizontalCenter: parent.horizontalCenter; style: "text"; text: qsTr("Cancel"); onClicked: msgCard.gm.cancelLogin() }
                    }

                    // Sign in
                    Column {
                        visible: msgCard.useGm && msgCard.gm.helperRunning && msgCard.signedOut
                        width: parent.width
                        spacing: 8
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            textFormat: Text.StyledText
                            color: Theme.surfaceVariantFg
                            font.pixelSize: Theme.bodyMedium
                            lineHeight: 1.2
                            text: qsTr("Google Messages needs your Google session cookies once:<br>"
                                     + "1. In a <b>private Firefox window</b>, open the sign-in page below and sign in. (Chrome's device-bound cookies usually don't work.)<br>"
                                     + "2. When messages.google.com shows its config page, press F12 → Network, reload, right-click the <b>config</b> request → Copy → <b>Copy as cURL</b>.<br>"
                                     + "3. Paste it here and press Connect, then tap the matching emoji on your phone. Close the private window afterwards without signing out.")
                        }
                        Row {
                            spacing: 8
                            Button3 {
                                style: "tonal"; iconName: "open_in_new"; text: qsTr("Open sign-in page")
                                onClicked: Qt.openUrlExternally("https://accounts.google.com/AccountChooser?continue=https://messages.google.com/web/config")
                            }
                            Button3 {
                                style: "text"; iconName: "content_copy"; text: qsTr("Copy link")
                                onClicked: { App.clipboard.copy("https://accounts.google.com/AccountChooser?continue=https://messages.google.com/web/config"); page.window.toast(qsTr("Copied")) }
                            }
                        }
                        Rectangle {
                            width: parent.width
                            height: 120
                            radius: Theme.radiusM
                            color: Theme.surfaceContainerHigh
                            B.ScrollView {
                                anchors.fill: parent
                                anchors.margins: 8
                                B.TextArea {
                                    id: cookieField
                                    placeholderText: qsTr("Paste the cURL command (or a Cookie header, or a JSON object of cookies)")
                                    placeholderTextColor: Theme.surfaceVariantFg
                                    color: Theme.surfaceFg
                                    font.family: "monospace"
                                    font.pixelSize: Theme.labelMedium
                                    wrapMode: TextEdit.WrapAnywhere
                                    background: null
                                    selectionColor: Theme.alpha(Theme.primary, 0.35)
                                }
                            }
                        }
                        Text {
                            width: parent.width
                            visible: text.length > 0
                            wrapMode: Text.Wrap
                            text: msgCard.gm.loginError
                            color: Theme.error
                            font.pixelSize: Theme.bodyMedium
                        }
                        Row {
                            spacing: 8
                            Button3 {
                                style: "filled"; iconName: "login"; text: qsTr("Connect")
                                enabled: cookieField.text.trim().length > 0
                                // The cookies go to the helper once and aren't kept in the field.
                                onClicked: { msgCard.gm.startLogin(cookieField.text); cookieField.text = "" }
                            }
                        }
                    }

                    OptionRow {
                        visible: msgCard.useGm && !msgCard.signedOut
                        title: qsTr("Take the session back automatically")
                        subtitle: qsTr("If Google Messages is opened in a browser, it becomes the active session and this computer stops getting messages. With this on, Slonnect takes it back after a few seconds.")
                        Switch3 {
                            checked: msgCard.gm.autoReactivate
                            onToggled: msgCard.gm.autoReactivate = checked
                        }
                    }
                }
            }

            // Devices by address
            SectionHeader { text: qsTr("Devices by address") }
            Card {
                width: parent.width
                style: "outlined"
                implicitHeight: addrCol.implicitHeight + 32
                Column {
                    id: addrCol
                    width: parent.width
                    spacing: 12
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("KDE Connect finds devices with broadcasts, which don't cross VPNs like Tailscale or other subnets. Add a device's IP address or host name to have this computer reach out to it directly.")
                        color: Theme.surfaceVariantFg
                        font.pixelSize: Theme.bodyMedium
                    }
                    Flow {
                        width: parent.width
                        spacing: 8
                        visible: App.host.customDevices.length > 0
                        Repeater {
                            model: App.host.customDevices
                            delegate: Chip {
                                required property string modelData
                                iconName: "lan"
                                text: modelData
                                removable: true
                                onRemoveClicked: App.host.removeCustomDevice(modelData)
                            }
                        }
                    }
                    Item {
                        width: parent.width
                        height: 44
                        Field {
                            id: addrField
                            anchors.left: parent.left
                            anchors.right: addAddr.left
                            anchors.rightMargin: 8
                            placeholder: qsTr("192.168.1.20 or phone.tailnet.ts.net")
                            onAccepted: addAddr.clicked()
                        }
                        Button3 {
                            id: addAddr
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            style: "filled"; iconName: "add"; text: qsTr("Add")
                            enabled: addrField.text.trim().length > 0
                            onClicked: {
                                if (App.host.addCustomDevice(addrField.text)) {
                                    addrField.text = ""
                                    App.devices.refreshDiscovery()
                                } else {
                                    page.window.toast(qsTr("That isn't an IP address or host name, or it's already listed"))
                                }
                            }
                        }
                    }
                }
            }

            // Plugins of the selected device
            Item {
                width: parent.width
                height: 48
                visible: !!page.device
                SectionHeader { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Plugins for %1").arg(page.deviceName) }
                SearchBar {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(280, parent.width / 2)
                    height: 40
                    placeholder: qsTr("Filter plugins")
                    onTextChanged: page.filter = text.toLowerCase()
                }
            }
            Text {
                width: parent.width
                visible: !!page.device && !page.device.reachable
                wrapMode: Text.Wrap
                text: qsTr("%1 is offline, so only plugins it offered last time are listed.").arg(page.deviceName)
                color: Theme.surfaceVariantFg
                font.pixelSize: Theme.bodyMedium
            }
            Card {
                width: parent.width
                visible: !!page.device
                style: "outlined"
                padding: 8
                implicitHeight: pluginCol.implicitHeight + 16
                Column {
                    id: pluginCol
                    width: parent.width
                    Repeater {
                        model: App.plugins
                        delegate: Column {
                            id: prow
                            required property string pluginId
                            required property bool enabled
                            required property bool loaded
                            readonly property var info: Features.pluginInfo(pluginId)
                            readonly property var form: page.configurable[pluginId]
                            readonly property bool open: page.expanded === pluginId && !!form
                            width: pluginCol.width
                            visible: !page.filter || (info.title + " " + info.description + " " + pluginId).toLowerCase().indexOf(page.filter) >= 0

                            Component.onCompleted: if (page.expanded === pluginId) scrollTimer.start()
                            Timer {
                                id: scrollTimer
                                interval: 100
                                onTriggered: scroll.B.ScrollBar.vertical.position = Math.max(0, Math.min(prow.mapToItem(content, 0, 0).y / content.height,
                                                                                                         1 - scroll.B.ScrollBar.vertical.size))
                            }

                            B.ItemDelegate {
                                id: pitem
                                width: parent.width
                                height: Math.max(64, ptexts.implicitHeight + 20)
                                hoverEnabled: true
                                onClicked: page.expanded = prow.open ? "" : (prow.form ? prow.pluginId : page.expanded)
                                background: Rectangle { radius: Theme.radiusM; color: "transparent"; StateLayer { hovered: pitem.hovered && !!prow.form; pressed: pitem.pressed && !!prow.form } }
                                contentItem: Item {
                                    Icon { id: picon; x: 8; anchors.verticalCenter: parent.verticalCenter; name: prow.info.icon; color: prow.enabled ? Theme.primary : Theme.surfaceVariantFg }
                                    Column {
                                        id: ptexts
                                        anchors.left: picon.right; anchors.leftMargin: 16
                                        anchors.right: ptrail.left; anchors.rightMargin: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 2
                                        Text { width: parent.width; text: prow.info.title; color: Theme.surfaceFg; font.pixelSize: Theme.bodyLarge; elide: Text.ElideRight }
                                        Text {
                                            width: parent.width
                                            text: prow.info.description + (prow.enabled && !prow.loaded && page.device && page.device.reachable
                                                                           ? " · " + qsTr("not active: %1 doesn't offer its side").arg(page.deviceName) : "")
                                            color: Theme.surfaceVariantFg; font.pixelSize: Theme.bodyMedium; wrapMode: Text.Wrap
                                        }
                                    }
                                    Row {
                                        id: ptrail
                                        anchors.right: parent.right; anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 8
                                        Icon {
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: !!prow.form
                                            name: prow.open ? "expand_less" : "expand_more"
                                        }
                                        Switch3 {
                                            anchors.verticalCenter: parent.verticalCenter
                                            checked: prow.enabled
                                            onToggled: App.plugins.setEnabled(prow.pluginId, checked)
                                        }
                                    }
                                }
                            }
                            Loader {
                                width: parent.width - 76
                                x: 56
                                active: prow.open
                                visible: active
                                sourceComponent: prow.form
                                onLoaded: item.width = Qt.binding(() => width)
                            }
                        }
                    }
                }
            }

            // About
            SectionHeader { text: qsTr("About") }
            Card {
                width: parent.width
                style: "outlined"
                implicitHeight: aboutCol.implicitHeight + 32
                Column {
                    id: aboutCol
                    width: parent.width
                    spacing: 8
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("Slonnect %1, a client for the KDE Connect daemon. Get KDE Connect for your phone:").arg(App.version)
                        color: Theme.surfaceFg
                        font.pixelSize: Theme.bodyMedium
                    }
                    Flow {
                        width: parent.width
                        spacing: 8
                        Button3 { style: "tonal"; iconName: "install_mobile"; text: qsTr("F-Droid"); onClicked: Qt.openUrlExternally("https://f-droid.org/packages/org.kde.kdeconnect_tp/") }
                        Button3 { style: "tonal"; iconName: "install_mobile"; text: qsTr("Google Play"); onClicked: Qt.openUrlExternally("https://play.google.com/store/apps/details?id=org.kde.kdeconnect_tp") }
                        Button3 { style: "tonal"; iconName: "install_mobile"; text: qsTr("App Store"); onClicked: Qt.openUrlExternally("https://apps.apple.com/app/kde-connect/id1580245991") }
                    }
                }
            }
        }
    }
}
