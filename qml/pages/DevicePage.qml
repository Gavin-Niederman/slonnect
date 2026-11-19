pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// Device status, quick actions, security details, and device switching/pairing.
Item {
    id: page
    property var window
    readonly property var device: App.device

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

            Text { text: qsTr("Device"); color: Theme.surfaceFg; font.pixelSize: Theme.displaySmall }

            // Current device hero card
            Card {
                width: parent.width
                visible: !!page.device
                color: Theme.primaryContainer
                implicitHeight: Math.max(136, heroText.implicitHeight + 32)
                Row {
                    anchors.fill: parent
                    spacing: 20
                    Rectangle {
                        width: 88; height: 88; radius: Theme.radiusXL
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.primary
                        Icon {
                            anchors.centerIn: parent; size: 48; filled: true; color: Theme.primaryFg
                            name: page.device && page.device.type === "tablet" ? "tablet_android" : "smartphone"
                        }
                    }
                    Column {
                        id: heroText
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        Text {
                            text: page.device ? page.device.name : ""
                            color: Theme.primaryContainerFg
                            font.pixelSize: Theme.headlineSmall
                            font.weight: Font.Medium
                        }
                        Row {
                            spacing: 16
                            Row {
                                spacing: 6
                                Icon { size: 18; color: Theme.primaryContainerFg; name: page.device && page.device.reachable ? "wifi" : "wifi_off"; anchors.verticalCenter: parent.verticalCenter }
                                Text {
                                    text: page.device && page.device.reachable ? qsTr("Connected") : qsTr("Offline")
                                    color: Theme.primaryContainerFg; font.pixelSize: Theme.bodyMedium
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            Row {
                                spacing: 6
                                visible: page.device && page.device.hasBattery
                                Icon {
                                    size: 18; color: Theme.primaryContainerFg; anchors.verticalCenter: parent.verticalCenter
                                    name: !page.device ? "battery_unknown" : page.device.charging ? "battery_charging_full"
                                        : page.device.charge < 15 ? "battery_alert" : page.device.charge >= 95 ? "battery_full"
                                        : "battery_" + [20, 20, 30, 30, 50, 50, 60, 80, 80, 90][Math.floor(page.device.charge / 10)]
                                }
                                Text {
                                    text: page.device ? page.device.charge + "%" + (page.device.charging ? qsTr(" · charging") : "") : ""
                                    color: Theme.primaryContainerFg; font.pixelSize: Theme.bodyMedium
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            Row {
                                spacing: 6
                                visible: !!page.device && page.device.reachable && page.device.hasConnectivity
                                Icon {
                                    size: 18; color: Theme.primaryContainerFg; anchors.verticalCenter: parent.verticalCenter
                                    name: !page.device || page.device.signalStrength < 0 ? "signal_cellular_off"
                                        : "signal_cellular_" + Math.min(4, page.device.signalStrength) + "_bar"
                                }
                                Text {
                                    text: !page.device ? "" : page.device.signalStrength < 0 ? qsTr("No signal") : page.device.networkType
                                    color: Theme.primaryContainerFg; font.pixelSize: Theme.bodyMedium
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                        Text {
                            visible: !!page.device && page.device.reachable && page.device.addresses.length > 0
                            text: page.device ? qsTr("%1 via %2").arg(page.device.addresses.join(", ")).arg(page.device.providers.join(", ")) : ""
                            color: Theme.primaryContainerFg; opacity: 0.8; font.pixelSize: Theme.bodyMedium
                        }
                    }
                }
            }

            // Quick actions
            Flow {
                width: parent.width
                spacing: 8
                visible: !!page.device && page.device.reachable
                Button3 {
                    style: "tonal"; iconName: "ring_volume"; text: qsTr("Ring phone")
                    visible: page.device && page.device.loadedPlugins.includes("kdeconnect_findmyphone")
                    onClicked: page.device.ring()
                }
                Button3 {
                    style: "tonal"; iconName: "notifications_active"; text: qsTr("Send ping")
                    visible: page.device && page.device.loadedPlugins.includes("kdeconnect_ping")
                    onClicked: { page.device.ping(); page.window.toast(qsTr("Ping sent")) }
                }
                Button3 {
                    style: "tonal"; iconName: "content_paste"; text: qsTr("Send clipboard")
                    visible: App.clipboard.available
                    onClicked: { App.clipboard.sendClipboard(); page.window.toast(qsTr("Clipboard sent")) }
                }
                Button3 {
                    style: "tonal"; iconName: "upload_file"; text: qsTr("Send files")
                    visible: App.share.available
                    onClicked: sendPicker.open()
                    FilePicker {
                        id: sendPicker
                        title: qsTr("Send files to %1").arg(page.device ? page.device.name : "")
                        onPicked: (urls) => { const n = App.share.shareUrls(urls); page.window.toast(qsTr("Sending %n file(s)", "", n)) }
                    }
                }
                Button3 {
                    style: "tonal"; iconName: App.lockDevice.locked ? "lock_open" : "lock"
                    text: App.lockDevice.locked ? qsTr("Unlock") : qsTr("Lock")
                    visible: App.lockDevice.available
                    onClicked: App.lockDevice.locked = !App.lockDevice.locked
                }
                Button3 {
                    style: "tonal"; iconName: "cast"
                    text: App.virtualMonitor.active ? qsTr("Stop virtual display") : qsTr("Use as a display")
                    visible: App.virtualMonitor.available
                    onClicked: App.virtualMonitor.active ? App.virtualMonitor.stop() : App.virtualMonitor.start()
                }
                Button3 {
                    style: "tonal"; iconName: "folder_open"; text: page.device && page.device.browsing ? qsTr("Mounting…") : qsTr("Browse files")
                    enabled: !(page.device && page.device.browsing)
                    visible: page.device && page.device.loadedPlugins.includes("kdeconnect_sftp")
                    onClicked: page.device.browseFiles()
                }
                Button3 {
                    style: "tonal"; iconName: "contacts"; text: App.contacts.syncing ? qsTr("Syncing contacts…") : qsTr("Sync contacts (%1)").arg(App.contacts.count)
                    visible: page.device && page.device.loadedPlugins.includes("kdeconnect_contacts")
                    enabled: !App.contacts.syncing
                    onClicked: App.contacts.sync()
                }
            }

            Text {
                width: parent.width
                visible: App.contacts.phoneSilent
                text: qsTr("Contact sync got no reply from %1. On the phone, open KDE Connect → this computer → Plugin settings and enable \"Contacts synchronizer\" (the Contacts permission alone isn't enough).").arg(page.device ? page.device.name : "")
                color: Theme.error
                font.pixelSize: Theme.bodyMedium
                wrapMode: Text.Wrap
            }

            // Screen mirroring (scrcpy over adb; independent of KDE Connect)
            Card {
                width: parent.width
                style: "filled"
                implicitHeight: mirrorRow.implicitHeight + 32
                Item {
                    id: mirrorRow
                    width: parent.width
                    implicitHeight: Math.max(mirrorText.implicitHeight, 40)
                    Column {
                        id: mirrorText
                        anchors.left: parent.left
                        anchors.right: mirrorActions.left
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text {
                            text: qsTr("Screen mirroring")
                            color: Theme.surfaceFg
                            font.pixelSize: Theme.titleMedium
                            font.weight: Font.Medium
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            color: Theme.surfaceVariantFg
                            font.pixelSize: Theme.bodyMedium
                            text: !App.mirror.toolsAvailable ? qsTr("Install scrcpy and android-tools to mirror and control your phone in a window.")
                                : App.mirror.state === "running" ? qsTr("Mirroring in its own window. Close it or press Stop to end.")
                                : qsTr("Show and control your phone in a window. Uses USB debugging, or Wireless debugging after \"adb pair\" + \"adb tcpip 5555\" (once per phone boot).")
                        }
                    }
                    Row {
                        id: mirrorActions
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Chip {
                            anchors.verticalCenter: parent.verticalCenter
                            checkable: true
                            checked: App.mirror.screenOff
                            iconName: "mobile_off"
                            text: qsTr("Phone screen off")
                            onClicked: App.mirror.screenOff = checked
                        }
                        Button3 {
                            anchors.verticalCenter: parent.verticalCenter
                            style: App.mirror.state === "running" ? "tonal" : "filled"
                            iconName: App.mirror.state === "running" ? "stop" : "screen_share"
                            text: App.mirror.state === "running" ? qsTr("Stop")
                                : App.mirror.state === "connecting" ? qsTr("Connecting…") : qsTr("Mirror screen")
                            enabled: App.mirror.toolsAvailable && App.mirror.state !== "connecting"
                            onClicked: App.mirror.state === "running" ? App.mirror.stop()
                                     : App.mirror.start(page.device ? page.device.name : "Phone", page.device ? page.device.addresses : [])
                        }
                    }
                }
            }

            // All devices
            Item {
                width: parent.width
                height: 40
                SectionHeader { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Devices") }
                Button3 {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    style: "text"; iconName: "refresh"; text: qsTr("Refresh")
                    onClicked: App.devices.refreshDiscovery()
                }
            }
            Card {
                width: parent.width
                style: "outlined"
                padding: 8
                implicitHeight: devicesCol.implicitHeight + 16
                Column {
                    id: devicesCol
                    width: parent.width
                    Repeater {
                        model: App.devices
                        delegate: Item {
                            id: drow
                            required property var device
                            required property string name
                            required property bool reachable
                            required property bool paired
                            readonly property bool current: App.device === device
                            width: devicesCol.width
                            height: 64
                            Icon {
                                id: dicon
                                x: 12
                                anchors.verticalCenter: parent.verticalCenter
                                name: drow.device.type === "desktop" ? "computer" : drow.device.type === "laptop" ? "laptop"
                                    : drow.device.type === "tablet" ? "tablet_android" : "smartphone"
                                color: drow.current ? Theme.primary : Theme.surfaceVariantFg
                                filled: drow.current
                            }
                            Column {
                                anchors.left: dicon.right
                                anchors.leftMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                Text { text: drow.name; color: Theme.surfaceFg; font.pixelSize: Theme.bodyLarge }
                                Text {
                                    text: (drow.paired ? qsTr("Paired") : drow.device.pairRequestedByPeer ? qsTr("Wants to pair · key %1").arg(drow.device.verificationKey)
                                                     : drow.device.pairRequested ? qsTr("Waiting for the other device…") : qsTr("Not paired"))
                                          + " · " + (drow.reachable ? qsTr("reachable") : qsTr("offline"))
                                    color: Theme.surfaceVariantFg
                                    font.pixelSize: Theme.bodyMedium
                                }
                            }
                            Row {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 4
                                Button3 {
                                    style: "text"; text: qsTr("Use")
                                    visible: drow.paired && !drow.current
                                    onClicked: App.devices.select(drow.device.id)
                                }
                                Button3 {
                                    style: "filled"; text: qsTr("Accept")
                                    visible: drow.device.pairRequestedByPeer
                                    onClicked: drow.device.acceptPairing()
                                }
                                Button3 {
                                    style: "text"; text: qsTr("Reject")
                                    visible: drow.device.pairRequestedByPeer
                                    onClicked: drow.device.cancelPairing()
                                }
                                Button3 {
                                    style: "tonal"; text: qsTr("Pair")
                                    visible: !drow.paired && drow.reachable && !drow.device.pairRequested && !drow.device.pairRequestedByPeer
                                    onClicked: drow.device.requestPairing()
                                }
                                Button3 {
                                    style: "text"; text: qsTr("Unpair")
                                    visible: drow.paired
                                    onClicked: drow.device.unpair()
                                }
                            }
                        }
                    }
                    Text {
                        visible: App.devices.count === 0
                        padding: 16
                        text: qsTr("No devices found. Make sure KDE Connect is running on both devices and they share a network.")
                        color: Theme.surfaceVariantFg
                        font.pixelSize: Theme.bodyMedium
                        wrapMode: Text.Wrap
                        width: parent.width
                    }
                }
            }

            // Security: what both sides compare when pairing, and the certificates.
            SectionHeader { text: qsTr("Security"); visible: !!page.device }
            Card {
                width: parent.width
                visible: !!page.device
                style: "outlined"
                implicitHeight: secCol.implicitHeight + 32
                Column {
                    id: secCol
                    width: parent.width
                    spacing: 8
                    property bool showDetails: false
                    Item {
                        width: parent.width
                        height: 40
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 12
                            Icon { name: "security"; anchors.verticalCenter: parent.verticalCenter; color: Theme.primary }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: page.device && page.device.verificationKey ? qsTr("Verification key %1").arg(page.device.verificationKey) : qsTr("Encrypted with TLS")
                                color: Theme.surfaceFg
                                font.pixelSize: Theme.bodyLarge
                            }
                        }
                        Button3 {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            style: "text"
                            text: secCol.showDetails ? qsTr("Hide details") : qsTr("Certificate details")
                            onClicked: { secCol.showDetails = !secCol.showDetails; if (secCol.showDetails) page.device.loadEncryptionInfo() }
                        }
                    }
                    Text {
                        width: parent.width
                        visible: secCol.showDetails
                        text: page.device ? page.device.encryptionInfo : ""
                        color: Theme.surfaceVariantFg
                        font.pixelSize: Theme.bodyMedium
                        font.family: "monospace"
                        wrapMode: Text.WrapAnywhere
                        textFormat: Text.PlainText
                    }
                }
            }

            Flow {
                width: parent.width
                spacing: 8
                Button3 {
                    style: "outlined"; iconName: "tune"; text: qsTr("Plugin settings")
                    visible: !!page.device
                    onClicked: page.window.go("settings")
                }
                Button3 {
                    style: "outlined"; iconName: "install_mobile"; text: qsTr("Get KDE Connect for the phone")
                    visible: App.devices.count === 0
                    onClicked: Qt.openUrlExternally("https://kdeconnect.kde.org/download.html")
                }
            }

            Text {
                topPadding: 16
                text: qsTr("Slonnect %1 · built on the KDE Connect daemon").arg(App.version)
                color: Theme.surfaceVariantFg
                font.pixelSize: Theme.labelMedium
            }
        }
    }
}
