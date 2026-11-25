pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import QtCore
import Slonnect

// Control the other device: touchpad and keyboard, presentation keys, the
// commands it has defined, and its volume.
Item {
    id: page
    property var window
    readonly property var device: App.device
    readonly property string deviceName: device ? device.name : qsTr("the device")
    readonly property bool isPhone: !device || device.type === "phone" || device.type === "tablet"
    readonly property bool anything: App.remoteControl.available || App.remoteKeyboard.available || App.remoteCommands.available || App.remoteVolume.available

    Settings {
        id: prefs
        category: "remote"
        property real speed: 1.5
    }

    // Keys that only make sense as modifiers of another key.
    function isModifier(key) {
        return [Qt.Key_Shift, Qt.Key_Control, Qt.Key_Alt, Qt.Key_Meta, Qt.Key_AltGr, Qt.Key_CapsLock, Qt.Key_NumLock, Qt.Key_Super_L, Qt.Key_Super_R].indexOf(key) >= 0
    }
    function forwardKey(event) {
        if (!App.remoteKeyboard.available || page.isModifier(event.key))
            return
        App.remoteKeyboard.sendKeyEvent(event.key, event.modifiers, event.text)
        event.accepted = true
    }

    B.ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth

        Column {
            width: Math.min(scroll.availableWidth - 48, 880)
            x: (scroll.availableWidth - width) / 2
            topPadding: 24
            bottomPadding: 24
            spacing: 16

            Text { text: qsTr("Remote"); color: Theme.surfaceFg; font.pixelSize: Theme.displaySmall }

            EmptyState {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !page.anything
                iconName: "touch_app"
                title: qsTr("Nothing to control")
                text: page.isPhone
                    ? qsTr("To use this computer as a touchpad and keyboard for %1, enable \"Remote input receiver\" in KDE Connect on the phone (it asks for accessibility access). Typing also needs KDE Connect's keyboard on the phone.").arg(page.deviceName)
                    : qsTr("%1 doesn't offer remote input, commands or volume control. Check its plugin settings.").arg(page.deviceName)
            }

            // --- touchpad -----------------------------------------------------
            Card {
                width: parent.width
                visible: App.remoteControl.available
                style: "filled"
                implicitHeight: padCol.implicitHeight + 32
                Column {
                    id: padCol
                    width: parent.width
                    spacing: 12
                    Item {
                        width: parent.width
                        height: 32
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Touchpad")
                            color: Theme.surfaceFg
                            font.pixelSize: Theme.titleMedium
                            font.weight: Font.Medium
                        }
                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            Text { text: qsTr("Speed"); color: Theme.surfaceVariantFg; font.pixelSize: Theme.labelLarge; anchors.verticalCenter: parent.verticalCenter }
                            Slider3 {
                                width: 140
                                from: 0.5; to: 4; stepSize: 0.25
                                value: prefs.speed
                                onMoved: prefs.speed = value
                            }
                        }
                    }

                    Rectangle {
                        id: pad
                        width: parent.width
                        height: 300
                        radius: Theme.radiusL
                        color: Theme.surfaceContainerLow
                        border.width: padArea.activeFocus ? 2 : 1
                        border.color: padArea.activeFocus ? Theme.primary : Theme.outlineVariant

                        Column {
                            anchors.centerIn: parent
                            spacing: 8
                            opacity: padArea.pressed ? 0.3 : 1
                            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "touch_app"; size: 40; color: Theme.surfaceVariantFg }
                            Text {
                                width: pad.width - 48
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                                text: qsTr("Drag to move the pointer. Click to click, right-click for a right click, wheel to scroll, press and hold to drag.")
                                      + (App.remoteKeyboard.available ? "\n" + qsTr("While this has focus, typing goes to %1.").arg(page.deviceName) : "")
                                color: Theme.surfaceVariantFg
                                font.pixelSize: Theme.bodyMedium
                            }
                        }

                        MouseArea {
                            id: padArea
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                            pressAndHoldInterval: 450
                            cursorShape: Qt.CrossCursor
                            activeFocusOnTab: true
                            property point last
                            property real travel
                            property bool dragging: false

                            onPressed: (mouse) => { forceActiveFocus(); last = Qt.point(mouse.x, mouse.y); travel = 0 }
                            onPositionChanged: (mouse) => {
                                const dx = mouse.x - last.x, dy = mouse.y - last.y
                                last = Qt.point(mouse.x, mouse.y)
                                travel += Math.abs(dx) + Math.abs(dy)
                                App.remoteControl.move(dx * prefs.speed, dy * prefs.speed)
                            }
                            onPressAndHold: (mouse) => {
                                if (mouse.button === Qt.LeftButton && travel < 6) { dragging = true; App.remoteControl.hold() }
                            }
                            onReleased: if (dragging) { dragging = false; App.remoteControl.release() }
                            onClicked: (mouse) => {
                                if (travel > 6)
                                    return
                                App.remoteControl.click(mouse.button === Qt.RightButton ? "right" : mouse.button === Qt.MiddleButton ? "middle" : "left")
                            }
                            // The first click already went out; one more makes it a double click.
                            onDoubleClicked: (mouse) => { if (mouse.button === Qt.LeftButton) App.remoteControl.click("left") }
                            onWheel: (wheel) => App.remoteControl.scroll(wheel.angleDelta.x, wheel.angleDelta.y)
                            Keys.onPressed: (event) => page.forwardKey(event)
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: 8
                        readonly property real w: (width - 16) / 3
                        Button3 { width: parent.w; style: "tonal"; text: qsTr("Left click"); onClicked: App.remoteControl.click("left") }
                        Button3 { width: parent.w; style: "tonal"; text: qsTr("Middle click"); onClicked: App.remoteControl.click("middle") }
                        Button3 { width: parent.w; style: "tonal"; text: qsTr("Right click"); onClicked: App.remoteControl.click("right") }
                    }
                }
            }

            // --- keyboard -----------------------------------------------------
            Card {
                width: parent.width
                visible: App.remoteKeyboard.available
                style: "filled"
                implicitHeight: kbCol.implicitHeight + 32
                Column {
                    id: kbCol
                    width: parent.width
                    spacing: 12
                    Row {
                        spacing: 12
                        Icon { name: "keyboard"; color: Theme.surfaceFg; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            text: qsTr("Keyboard")
                            color: Theme.surfaceFg
                            font.pixelSize: Theme.titleMedium
                            font.weight: Font.Medium
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Chip {
                            visible: page.isPhone
                            enabled: false
                            opacity: 1
                            iconName: App.remoteKeyboard.remoteState ? "check" : "info"
                            text: App.remoteKeyboard.remoteState ? qsTr("KDE Connect keyboard active") : qsTr("Phone keyboard not ready")
                        }
                    }
                    Text {
                        width: parent.width
                        visible: page.isPhone && !App.remoteKeyboard.remoteState
                        wrapMode: Text.Wrap
                        text: qsTr("On %1, open the text field you want to type into and switch the keyboard to \"KDE Connect Remote Keyboard\" (enable it once in Android's keyboard settings).").arg(page.deviceName)
                        color: Theme.surfaceVariantFg
                        font.pixelSize: Theme.bodyMedium
                    }

                    // Live typing: every key press goes straight through.
                    Rectangle {
                        width: parent.width
                        height: 48
                        radius: 24
                        color: Theme.surfaceContainerHigh
                        border.width: liveKeys.activeFocus ? 2 : 0
                        border.color: Theme.primary
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 20
                            anchors.verticalCenter: parent.verticalCenter
                            text: liveKeys.activeFocus ? qsTr("Typing on %1 — Tab or click elsewhere to stop").arg(page.deviceName)
                                                       : qsTr("Click here, then type: keys go straight to %1").arg(page.deviceName)
                            color: liveKeys.activeFocus ? Theme.primary : Theme.surfaceVariantFg
                            font.pixelSize: Theme.bodyMedium
                        }
                        Item {
                            id: liveKeys
                            anchors.fill: parent
                            activeFocusOnTab: true
                            Keys.onPressed: (event) => {
                                if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
                                    return // let focus move on
                                page.forwardKey(event)
                            }
                            TapHandler { onTapped: liveKeys.forceActiveFocus() }
                        }
                    }

                    // Whole text at once (pasting a password, a URL, ...).
                    Item {
                        width: parent.width
                        height: 48
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: typeButton.left
                            anchors.rightMargin: 8
                            height: 48
                            radius: 24
                            color: Theme.surfaceContainerHigh
                            B.TextField {
                                id: typeField
                                anchors.fill: parent
                                anchors.leftMargin: 20
                                anchors.rightMargin: 20
                                background: null
                                color: Theme.surfaceFg
                                placeholderText: qsTr("Or write text here and send it in one go")
                                placeholderTextColor: Theme.surfaceVariantFg
                                selectionColor: Theme.alpha(Theme.primary, 0.35)
                                font.pixelSize: Theme.bodyMedium
                                onAccepted: typeButton.clicked()
                            }
                        }
                        Button3 {
                            id: typeButton
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            style: "filled"
                            iconName: "keyboard_return"
                            text: qsTr("Type")
                            enabled: typeField.text.length > 0
                            onClicked: { App.remoteKeyboard.sendText(typeField.text); typeField.text = "" }
                        }
                    }
                }
            }

            // --- presentation (desktops: the phone has its own presenter) ----
            Card {
                width: parent.width
                visible: App.remoteKeyboard.available && !page.isPhone
                style: "filled"
                implicitHeight: presCol.implicitHeight + 32
                Column {
                    id: presCol
                    width: parent.width
                    spacing: 12
                    Row {
                        spacing: 12
                        Icon { name: "slideshow"; color: Theme.surfaceFg; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: qsTr("Presentation"); color: Theme.surfaceFg; font.pixelSize: Theme.titleMedium; font.weight: Font.Medium; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Row {
                        width: parent.width
                        spacing: 8
                        readonly property real w: (width - 24) / 4
                        // KDE Connect special keys: 8 PageUp, 9 PageDown, 25 F5, 14 Escape.
                        Button3 { width: parent.w; style: "tonal"; iconName: "fullscreen"; text: qsTr("Start"); onClicked: App.remoteKeyboard.sendSpecialKey(25) }
                        Button3 { width: parent.w; style: "filled"; iconName: "navigate_before"; text: qsTr("Previous"); onClicked: App.remoteKeyboard.sendSpecialKey(8) }
                        Button3 { width: parent.w; style: "filled"; iconName: "navigate_next"; text: qsTr("Next"); onClicked: App.remoteKeyboard.sendSpecialKey(9) }
                        Button3 { width: parent.w; style: "tonal"; iconName: "close"; text: qsTr("End"); onClicked: App.remoteKeyboard.sendSpecialKey(14) }
                    }
                }
            }

            // --- commands defined on the other device ------------------------
            Item {
                width: parent.width
                height: 40
                visible: App.remoteCommands.available
                SectionHeader { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Commands on %1").arg(page.deviceName) }
                Button3 {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    style: "text"; iconName: "edit"; text: qsTr("Edit on %1").arg(page.deviceName)
                    visible: App.remoteCommands.canAddCommand
                    onClicked: { App.remoteCommands.editCommands(); page.window.toast(qsTr("Opened the command editor on %1").arg(page.deviceName)) }
                }
            }
            Card {
                width: parent.width
                visible: App.remoteCommands.available
                style: "outlined"
                padding: 8
                implicitHeight: cmdCol.implicitHeight + 16
                Column {
                    id: cmdCol
                    width: parent.width
                    Repeater {
                        model: App.remoteCommands.commands
                        delegate: B.ItemDelegate {
                            id: cmd
                            required property var modelData
                            width: cmdCol.width
                            height: 60
                            hoverEnabled: true
                            onClicked: { App.remoteCommands.trigger(modelData.key); page.window.toast(qsTr("Ran \"%1\"").arg(modelData.name)) }
                            background: Rectangle { radius: Theme.radiusM; color: "transparent"; StateLayer { hovered: cmd.hovered; pressed: cmd.pressed } }
                            contentItem: Item {
                                Icon { id: cmdIcon; x: 8; anchors.verticalCenter: parent.verticalCenter; name: "terminal" }
                                Column {
                                    anchors.left: cmdIcon.right; anchors.leftMargin: 16
                                    anchors.right: runIcon.left; anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    Text { width: parent.width; text: cmd.modelData.name; color: Theme.surfaceFg; font.pixelSize: Theme.bodyLarge; elide: Text.ElideRight }
                                    Text { width: parent.width; text: cmd.modelData.command; color: Theme.surfaceVariantFg; font.pixelSize: Theme.bodyMedium; font.family: "monospace"; elide: Text.ElideRight }
                                }
                                Icon { id: runIcon; anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; name: "play_arrow"; color: Theme.primary }
                            }
                        }
                    }
                    Text {
                        visible: App.remoteCommands.commands.length === 0
                        width: parent.width
                        padding: 12
                        wrapMode: Text.Wrap
                        text: qsTr("%1 hasn't defined any commands.").arg(page.deviceName)
                        color: Theme.surfaceVariantFg
                        font.pixelSize: Theme.bodyMedium
                    }
                }
            }

            // --- the other device's volume -------------------------------------
            SectionHeader { visible: App.remoteVolume.available; text: qsTr("Volume on %1").arg(page.deviceName) }
            Card {
                width: parent.width
                visible: App.remoteVolume.available
                style: "outlined"
                implicitHeight: volCol.implicitHeight + 32
                Column {
                    id: volCol
                    width: parent.width
                    spacing: 12
                    Repeater {
                        model: App.remoteVolume.sinks
                        delegate: Column {
                            id: sink
                            required property var modelData
                            property int volume: modelData.volume
                            property bool muted: modelData.muted
                            width: volCol.width
                            spacing: 4
                            Connections {
                                target: App.remoteVolume
                                function onSinkUpdated(name, volume, muted) {
                                    if (name !== sink.modelData.name)
                                        return
                                    if (!slider.pressed)
                                        sink.volume = volume
                                    sink.muted = muted
                                }
                            }
                            Text { width: parent.width; text: sink.modelData.description || sink.modelData.name; color: Theme.surfaceFg; font.pixelSize: Theme.bodyLarge; elide: Text.ElideRight }
                            Row {
                                width: parent.width
                                spacing: 8
                                IconButton {
                                    iconName: sink.muted || sink.volume === 0 ? "volume_off" : "volume_up"
                                    tooltip: sink.muted ? qsTr("Unmute") : qsTr("Mute")
                                    onClicked: App.remoteVolume.setMuted(sink.modelData.name, !sink.muted)
                                }
                                Slider3 {
                                    id: slider
                                    width: parent.width - 48 - 56
                                    anchors.verticalCenter: parent.verticalCenter
                                    from: 0; to: Math.max(1, sink.modelData.maxVolume); stepSize: 1
                                    value: sink.volume
                                    accent: sink.muted ? Theme.outline : Theme.primary
                                    onMoved: { sink.volume = value; App.remoteVolume.setVolume(sink.modelData.name, value) }
                                }
                                Text {
                                    width: 48
                                    anchors.verticalCenter: parent.verticalCenter
                                    horizontalAlignment: Text.AlignRight
                                    text: Math.round(100 * sink.volume / Math.max(1, sink.modelData.maxVolume)) + "%"
                                    color: Theme.surfaceVariantFg
                                    font.pixelSize: Theme.labelLarge
                                }
                            }
                        }
                    }
                    Text {
                        visible: App.remoteVolume.sinks.length === 0
                        text: qsTr("No audio outputs reported yet.")
                        color: Theme.surfaceVariantFg
                        font.pixelSize: Theme.bodyMedium
                    }
                }
            }
        }
    }
}
