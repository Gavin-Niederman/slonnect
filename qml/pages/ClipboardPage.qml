pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

Item {
    id: page
    property var window
    readonly property string deviceName: App.device ? App.device.name : qsTr("phone")

    ListView {
        id: list
        anchors.fill: parent
        model: App.clipboard.history
        reuseItems: true
        clip: true
        spacing: 8
        topMargin: 24
        bottomMargin: 24
        B.ScrollBar.vertical: B.ScrollBar {}
        readonly property real columnWidth: Math.min(width - 48, 880)
        // Margins centre the whole content column (header, rows, footer).
        leftMargin: (width - columnWidth) / 2
        rightMargin: leftMargin

        header: Column {
            width: list.columnWidth
            spacing: 16
            bottomPadding: 8

            Text {
                text: qsTr("Clipboard")
                color: Theme.surfaceFg
                font.pixelSize: Theme.displaySmall
            }

            Card {
                width: list.columnWidth
                style: "filled"
                color: Theme.primaryContainer
                implicitHeight: currentCol.implicitHeight + 32
                Column {
                    id: currentCol
                    width: parent.width
                    spacing: 12
                    Row {
                        spacing: 8
                        Icon { name: "content_paste"; color: Theme.primaryContainerFg; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            text: qsTr("On this computer's clipboard")
                            color: Theme.primaryContainerFg
                            font.pixelSize: Theme.titleMedium
                            font.weight: Font.Medium
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    Text {
                        width: parent.width
                        text: App.clipboard.current || qsTr("Nothing copied yet")
                        maximumLineCount: 4
                        elide: Text.ElideRight
                        wrapMode: Text.Wrap
                        color: Theme.primaryContainerFg
                        font.pixelSize: Theme.bodyLarge
                        font.family: /\n/.test(App.clipboard.current) ? "monospace" : Qt.application.font.family
                    }
                    Row {
                        spacing: 8
                        Button3 {
                            style: "filled"
                            iconName: "send_to_mobile"
                            text: qsTr("Send to %1").arg(page.deviceName)
                            enabled: App.clipboard.available && App.clipboard.current.length > 0
                            onClicked: { App.clipboard.sendClipboard(); page.window.toast(qsTr("Clipboard sent to %1").arg(page.deviceName)) }
                        }
                        Chip {
                            anchors.verticalCenter: parent.verticalCenter
                            enabled: false
                            opacity: 1
                            iconName: App.clipboard.autoShare ? "sync" : "sync_disabled"
                            text: App.clipboard.autoShare ? qsTr("Auto-sync on") : qsTr("Auto-sync off")
                        }
                    }
                }
            }

            Item {
                width: list.columnWidth
                height: 40
                SectionHeader { anchors.verticalCenter: parent.verticalCenter; text: qsTr("History") }
                Button3 {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    style: "text"
                    text: qsTr("Clear all")
                    visible: list.count > 0
                    onClicked: App.clipboard.clearHistory()
                }
            }
        }

        delegate: B.ItemDelegate {
            id: item
            required property string modelData
            required property int index
            width: list.columnWidth
            height: Math.max(64, textItem.implicitHeight + 24)
            hoverEnabled: true
            onClicked: { App.clipboard.copy(modelData); page.window.toast(qsTr("Copied")) }
            background: Rectangle {
                radius: Theme.radiusL
                color: Theme.surfaceContainerLow
                StateLayer { hovered: item.hovered; pressed: item.pressed }
            }
            contentItem: Item {
                Text {
                    id: textItem
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.right: itemActions.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: item.modelData
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    wrapMode: Text.Wrap
                    color: Theme.surfaceFg
                    font.pixelSize: Theme.bodyMedium
                    textFormat: Text.PlainText
                }
                Row {
                    id: itemActions
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: item.hovered ? 1 : 0.6
                    IconButton {
                        iconName: "send_to_mobile"
                        tooltip: qsTr("Send to %1").arg(page.deviceName)
                        enabled: App.clipboard.available
                        onClicked: { App.clipboard.sendText(item.modelData); page.window.toast(qsTr("Sent to %1").arg(page.deviceName)) }
                    }
                    IconButton { iconName: "delete"; tooltip: qsTr("Remove"); onClicked: App.clipboard.remove(item.index) }
                }
            }
        }

        footer: Text {
            width: list.columnWidth
            topPadding: 16
            text: qsTr("Text copied on the phone shows up here automatically when auto-sync is on. On Wayland, desktop copies are recorded while Slonnect is focused.")
            color: Theme.surfaceVariantFg
            font.pixelSize: Theme.bodyMedium
            wrapMode: Text.Wrap
        }
    }
}
