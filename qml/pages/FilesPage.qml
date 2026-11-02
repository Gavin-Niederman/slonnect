pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// Send files / links / text to the phone and list what the phone sent here.
Item {
    id: page
    property var window
    readonly property string deviceName: App.device ? App.device.name : qsTr("your phone")

    function sendUrls(urls) {
        const n = App.share.shareUrls(urls)
        if (n > 0)
            page.window.toast(qsTr("Sending %n file(s) to %1", "", n).arg(page.deviceName))
    }

    B.ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth

        Column {
            id: column
            width: Math.min(scroll.availableWidth - 48, 880)
            x: (scroll.availableWidth - width) / 2
            topPadding: 24
            bottomPadding: 24
            spacing: 16

            Text {
                text: qsTr("Files")
                color: Theme.surfaceFg
                font.pixelSize: Theme.displaySmall
            }

            // Drop zone
            Rectangle {
                id: dropZone
                width: parent.width
                height: 220
                radius: Theme.radiusXL
                color: drop.containsDrag ? Theme.primaryContainer : Theme.surfaceContainerLow
                border.width: 2
                border.color: drop.containsDrag ? Theme.primary : Theme.outlineVariant
                Behavior on color { ColorAnimation { duration: Theme.durationShort } }

                Column {
                    anchors.centerIn: parent
                    spacing: 12
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: "upload_file"
                        size: 48
                        color: drop.containsDrag ? Theme.primaryContainerFg : Theme.primary
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: App.share.available ? qsTr("Drop files here to send them to %1").arg(page.deviceName)
                                                  : qsTr("File sharing is unavailable while the phone is disconnected")
                        color: Theme.surfaceFg
                        font.pixelSize: Theme.titleMedium
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 8
                        Button3 {
                            style: "filled"
                            iconName: "attach_file"
                            text: qsTr("Choose files")
                            enabled: App.share.available
                            onClicked: fileDialog.open()
                        }
                        Button3 {
                            style: "outlined"
                            iconName: "folder_open"
                            text: App.device && App.device.browsing ? qsTr("Mounting…") : qsTr("Browse phone")
                            enabled: !(App.device && App.device.browsing)
                            visible: App.device && App.device.loadedPlugins.includes("kdeconnect_sftp")
                            onClicked: App.device.browseFiles()
                        }
                    }
                }
                DropArea {
                    id: drop
                    anchors.fill: parent
                    enabled: App.share.available
                    onDropped: (d) => { page.sendUrls(d.urls); d.accept(Qt.CopyAction) }
                }
            }

            // Send link / text
            Card {
                width: parent.width
                style: "filled"
                implicitHeight: 88
                Row {
                    anchors.fill: parent
                    spacing: 12
                    Icon { name: "link"; anchors.verticalCenter: parent.verticalCenter; color: Theme.primary }
                    Rectangle {
                        width: parent.width - 36 - 12 - sendText.width - 12
                        height: 48
                        radius: 24
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.surfaceContainerLowest
                        B.TextField {
                            id: textField
                            anchors.fill: parent
                            leftPadding: 20
                            rightPadding: 20
                            background: null
                            color: Theme.surfaceFg
                            placeholderText: qsTr("Paste a link to open on the phone, or any text")
                            placeholderTextColor: Theme.surfaceVariantFg
                            font.pixelSize: Theme.bodyLarge
                            selectByMouse: true
                            onAccepted: sendText.clicked()
                        }
                    }
                    Button3 {
                        id: sendText
                        anchors.verticalCenter: parent.verticalCenter
                        style: "tonal"
                        iconName: "send"
                        text: qsTr("Send")
                        enabled: App.share.available && textField.text.trim().length > 0
                        onClicked: {
                            App.share.shareText(textField.text)
                            page.window.toast(qsTr("Sent to %1").arg(page.deviceName))
                            textField.text = ""
                        }
                    }
                }
            }

            // Received
            Item {
                width: parent.width
                height: 48
                SectionHeader {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Received from %1").arg(page.deviceName)
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    Button3 { style: "text"; iconName: "folder"; text: qsTr("Open folder"); onClicked: App.share.openIncomingFolder() }
                    Button3 { style: "text"; text: qsTr("Clear"); visible: App.share.received.length > 0; onClicked: App.share.clearReceived() }
                }
            }

            Text {
                visible: App.share.received.length === 0
                width: parent.width
                text: qsTr("Files you share from the phone land in %1.").arg(App.share.incomingFolder)
                color: Theme.surfaceVariantFg
                font.pixelSize: Theme.bodyMedium
                wrapMode: Text.Wrap
            }

            Column {
                width: parent.width
                Repeater {
                    model: App.share.received
                    delegate: B.ItemDelegate {
                        id: row
                        required property var modelData
                        width: column.width
                        height: 64
                        hoverEnabled: true
                        onClicked: App.share.openReceived(modelData.url)
                        background: Rectangle {
                            radius: Theme.radiusM
                            color: "transparent"
                            StateLayer { hovered: row.hovered; pressed: row.pressed }
                        }
                        contentItem: Item {
                            Rectangle {
                                id: fileIcon
                                width: 40; height: 40; radius: Theme.radiusM
                                anchors.verticalCenter: parent.verticalCenter
                                x: 8
                                color: Theme.secondaryContainer
                                Icon {
                                    anchors.centerIn: parent
                                    color: Theme.secondaryContainerFg
                                    name: !row.modelData.isFile ? "link"
                                        : /\.(png|jpe?g|gif|webp|heic)$/i.test(row.modelData.name) ? "image"
                                        : /\.(mp4|mkv|webm|mov)$/i.test(row.modelData.name) ? "movie"
                                        : /\.(mp3|m4a|ogg|flac|wav|opus)$/i.test(row.modelData.name) ? "music_note"
                                        : /\.pdf$/i.test(row.modelData.name) ? "picture_as_pdf"
                                        : "insert_drive_file"
                                }
                            }
                            Column {
                                anchors.left: fileIcon.right
                                anchors.leftMargin: 16
                                anchors.right: rowActions.left
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                Text { width: parent.width; text: row.modelData.name; elide: Text.ElideMiddle; color: Theme.surfaceFg; font.pixelSize: Theme.bodyLarge }
                                Text {
                                    width: parent.width
                                    text: (row.modelData.isFile ? App.formatSize(row.modelData.size) + " · " : "") + Theme.shortStamp(row.modelData.time)
                                    color: Theme.surfaceVariantFg
                                    font.pixelSize: Theme.bodyMedium
                                }
                            }
                            Row {
                                id: rowActions
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                IconButton { iconName: "folder_open"; tooltip: qsTr("Show in folder"); visible: row.modelData.isFile; onClicked: App.share.revealReceived(row.modelData.url) }
                                IconButton { iconName: "open_in_new"; tooltip: qsTr("Open"); onClicked: App.share.openReceived(row.modelData.url) }
                            }
                        }
                    }
                }
            }
        }
    }

    FilePicker {
        id: fileDialog
        title: qsTr("Send files to %1").arg(page.deviceName)
        onPicked: (urls) => page.sendUrls(urls)
    }
}
