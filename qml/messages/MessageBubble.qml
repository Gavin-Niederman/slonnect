import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// One message row: optional day chip, sender label, avatar and bubble.
Item {
    id: root
    required property int index
    required property string body
    required property var date
    required property bool outgoing
    required property string status
    required property string sender
    required property string senderName
    required property string senderAvatar
    required property var attachments
    required property bool showDay
    required property bool groupStart
    required property bool groupEnd
    required property bool multi
    signal toast(string text)

    readonly property int sideMargin: 16
    readonly property int avatarSlot: outgoing ? 0 : 40
    readonly property real maxBubble: Math.min(width * 0.72, 560)
    readonly property bool hasBody: body.length > 0
    readonly property color bubbleColor: status === "failed" ? Theme.errorContainer
                                       : outgoing ? Theme.primary : Theme.surfaceContainerHigh
    readonly property color textColor: status === "failed" ? Theme.errorContainerFg
                                     : outgoing ? Theme.primaryFg : Theme.surfaceFg

    implicitHeight: column.implicitHeight + (groupStart ? 8 : 2)

    Column {
        id: column
        y: root.groupStart ? 8 : 2
        width: parent.width
        spacing: 2

        // Day separator chip
        Item {
            visible: root.showDay
            width: parent.width
            height: visible ? 40 : 0
            Rectangle {
                anchors.centerIn: parent
                width: dayText.implicitWidth + 24
                height: 24
                radius: 12
                color: Theme.surfaceContainer
                Text {
                    id: dayText
                    anchors.centerIn: parent
                    text: Theme.dayLabel(root.date)
                    color: Theme.surfaceVariantFg
                    font.pixelSize: Theme.labelMedium
                    font.weight: Font.Medium
                }
            }
        }

        Text {
            visible: root.multi && !root.outgoing && root.groupStart
            x: root.sideMargin + root.avatarSlot + 12
            text: root.senderName
            color: Theme.surfaceVariantFg
            font.pixelSize: Theme.labelMedium
            bottomPadding: 2
        }

        Item {
            width: parent.width
            height: bubble.height

            Avatar {
                visible: !root.outgoing && root.groupEnd
                x: root.sideMargin
                anchors.bottom: parent.bottom
                size: 32
                name: root.senderName
                source: root.senderAvatar
            }

            Rectangle {
                id: bubble
                readonly property real big: 20
                readonly property real small: 4
                x: root.outgoing ? parent.width - width - root.sideMargin : root.sideMargin + root.avatarSlot
                width: Math.max(content.width, 40)
                height: content.height
                color: root.hasBody ? root.bubbleColor : "transparent"
                // Grouped messages get tight corners on the side facing their siblings.
                topLeftRadius: !root.outgoing && !root.groupStart ? small : big
                bottomLeftRadius: !root.outgoing && !root.groupEnd ? small : big
                topRightRadius: root.outgoing && !root.groupStart ? small : big
                bottomRightRadius: root.outgoing && !root.groupEnd ? small : big
                opacity: root.status === "pending" ? 0.7 : 1

                Column {
                    id: content
                    padding: root.hasBody ? 0 : 0
                    spacing: 4

                    Repeater {
                        model: root.attachments
                        delegate: AttachmentView {
                            maxWidth: root.maxBubble
                            outgoing: root.outgoing
                            onToast: (text) => root.toast(text)
                        }
                    }

                    Text {
                        id: bodyText
                        visible: root.hasBody
                        width: Math.min(implicitWidth, root.maxBubble)
                        leftPadding: 16
                        rightPadding: 16
                        topPadding: 10
                        bottomPadding: 10
                        text: root.hasBody ? Theme.linkify(root.body, root.outgoing ? Theme.primaryFg : Theme.primary) : ""
                        textFormat: Text.StyledText
                        wrapMode: Text.Wrap
                        color: root.textColor
                        font.pixelSize: Theme.bodyLarge
                        linkColor: root.outgoing ? Theme.primaryFg : Theme.primary
                        onLinkActivated: (link) => Qt.openUrlExternally(link)
                        HoverHandler { cursorShape: bodyText.hoveredLink ? Qt.PointingHandCursor : Qt.IBeamCursor }
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: menuLoader.active = true
                }
                Loader {
                    id: menuLoader
                    active: false
                    onLoaded: item.popup()
                    sourceComponent: B.Menu {
                        onClosed: menuLoader.active = false
                        B.MenuItem {
                            text: qsTr("Copy text")
                            enabled: root.hasBody
                            onTriggered: { App.clipboard.copy(root.body); root.toast(qsTr("Copied")) }
                        }
                        B.MenuItem {
                            text: qsTr("Send text to phone clipboard")
                            enabled: root.hasBody && App.clipboard.available
                            onTriggered: { App.clipboard.sendText(root.body); root.toast(qsTr("Sent to phone clipboard")) }
                        }
                    }
                }
            }
        }

        // Timestamp + delivery state under the last bubble of a run.
        Text {
            visible: root.groupEnd
            anchors.right: root.outgoing ? parent.right : undefined
            anchors.rightMargin: root.sideMargin + 4
            x: root.outgoing ? 0 : root.sideMargin + root.avatarSlot + 4
            topPadding: 2
            bottomPadding: 4
            text: root.status === "pending" ? qsTr("Sending…")
                : root.status === "failed" ? qsTr("Not sent")
                : Theme.timeOfDay(root.date)
            color: root.status === "failed" ? Theme.error : Theme.surfaceVariantFg
            font.pixelSize: Theme.labelSmall
        }
    }
}
