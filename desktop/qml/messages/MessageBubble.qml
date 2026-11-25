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
    required property string messageId
    required property string error
    required property var reactions
    required property var reply
    signal toast(string text)

    readonly property var quickReactions: ["👍", "❤️", "😂", "😮", "😢", "🙏"]
    readonly property bool canAct: messageId.length > 0 && status !== "pending" && status !== "failed"

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
            id: bubbleRow
            width: parent.width
            height: bubble.height

            HoverHandler { id: rowHover }

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

                    // The message this one answers.
                    Rectangle {
                        visible: !!root.reply && root.reply.body !== undefined
                        x: 8
                        width: Math.min(Math.max(quoteSender.implicitWidth, quoteBody.implicitWidth, 100) + 24, root.maxBubble - 16)
                        height: visible ? quoteCol.implicitHeight + 12 : 0
                        radius: 12
                        color: root.outgoing ? Theme.alpha(Theme.primaryFg, 0.15) : Theme.alpha(Theme.surfaceFg, 0.07)
                        Rectangle { width: 3; radius: 2; height: parent.height - 12; anchors.verticalCenter: parent.verticalCenter; x: 6; color: root.outgoing ? Theme.primaryFg : Theme.primary }
                        Column {
                            id: quoteCol
                            x: 14; y: 6
                            width: parent.width - 20
                            Text {
                                id: quoteSender
                                width: parent.width
                                text: root.reply ? root.reply.sender || "" : ""
                                visible: text.length > 0
                                color: root.textColor
                                font.pixelSize: Theme.labelMedium
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Text {
                                id: quoteBody
                                width: parent.width
                                text: root.reply ? root.reply.body || "" : ""
                                color: root.textColor
                                opacity: 0.8
                                font.pixelSize: Theme.bodyMedium
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                wrapMode: Text.Wrap
                                textFormat: Text.PlainText
                            }
                        }
                    }

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
                TapHandler {
                    enabled: App.sms.canReact && root.canAct
                    onDoubleTapped: App.sms.react(root.messageId, "❤️")
                }
                Loader {
                    id: menuLoader
                    active: false
                    onLoaded: item.popup()
                    sourceComponent: B.Menu {
                        onClosed: menuLoader.active = false
                        B.MenuItem {
                            text: qsTr("Reply")
                            visible: App.sms.canReply && root.canAct
                            height: visible ? implicitHeight : 0
                            onTriggered: App.sms.setReplyTo(root.messageId)
                        }
                        Row {
                            visible: App.sms.canReact && root.canAct
                            height: visible ? 40 : 0
                            leftPadding: 8
                            Repeater {
                                model: root.quickReactions
                                delegate: B.ToolButton {
                                    required property string modelData
                                    text: modelData
                                    font.pixelSize: 20
                                    onClicked: { App.sms.react(root.messageId, modelData); menuLoader.item.close() }
                                }
                            }
                        }
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

            // Reply / react, next to the bubble while hovering.
            Row {
                visible: rowHover.hovered && root.canAct && (App.sms.canReply || App.sms.canReact)
                anchors.verticalCenter: bubble.verticalCenter
                x: root.outgoing ? bubble.x - width - 4 : bubble.x + bubble.width + 4
                IconButton {
                    visible: App.sms.canReact
                    iconName: "add_reaction"; iconSize: 20; implicitWidth: 32; implicitHeight: 32
                    tooltip: qsTr("React")
                    onClicked: menuLoader.active = true
                }
                IconButton {
                    visible: App.sms.canReply
                    iconName: "reply"; iconSize: 20; implicitWidth: 32; implicitHeight: 32
                    tooltip: qsTr("Reply")
                    onClicked: App.sms.setReplyTo(root.messageId)
                }
            }
        }

        // Reactions, tucked under the bubble. Tap one to add or take back yours.
        Flow {
            visible: root.reactions && root.reactions.length > 0
            width: Math.min(root.maxBubble, parent.width - 2 * root.sideMargin)
            x: root.outgoing ? parent.width - root.sideMargin - Math.min(width, childrenRect.width) - 8 : root.sideMargin + root.avatarSlot + 8
            spacing: 4
            topPadding: 2
            Repeater {
                model: root.reactions || []
                delegate: B.AbstractButton {
                    id: chip
                    required property var modelData
                    implicitHeight: 26
                    implicitWidth: chipText.implicitWidth + 16
                    hoverEnabled: true
                    onClicked: App.sms.react(root.messageId, modelData.emoji)
                    background: Rectangle {
                        radius: 13
                        color: chip.modelData.mine ? Theme.secondaryContainer : Theme.surfaceContainerHighest
                        border.width: 1
                        border.color: Theme.surface
                        StateLayer { hovered: chip.hovered; pressed: chip.pressed }
                    }
                    contentItem: Text {
                        id: chipText
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: chip.modelData.emoji + (chip.modelData.count > 1 ? " " + chip.modelData.count : "")
                        color: chip.modelData.mine ? Theme.secondaryContainerFg : Theme.surfaceFg
                        font.pixelSize: Theme.labelLarge
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
                : root.status === "failed" ? (root.error ? qsTr("Not sent: %1").arg(root.error) : qsTr("Not sent"))
                : root.status === "read" ? qsTr("%1 · Read").arg(Theme.timeOfDay(root.date))
                : root.status === "delivered" ? qsTr("%1 · Delivered").arg(Theme.timeOfDay(root.date))
                : Theme.timeOfDay(root.date)
            color: root.status === "failed" ? Theme.error : Theme.surfaceVariantFg
            font.pixelSize: Theme.labelSmall
        }
    }
}
