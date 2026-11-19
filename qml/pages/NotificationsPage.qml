pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// The phone's notifications: read, reply, dismiss. kdeconnectd also pops each
// one up as a desktop notification; this is the list that stays.
Item {
    id: page
    property var window
    readonly property var plugin: App.notifications
    readonly property string deviceName: App.device ? App.device.name : qsTr("your phone")

    ListView {
        id: list
        anchors.fill: parent
        model: page.plugin.model
        clip: true
        spacing: 8
        topMargin: 24
        bottomMargin: 24
        B.ScrollBar.vertical: B.ScrollBar {}
        readonly property real columnWidth: Math.min(width - 48, 880)
        leftMargin: (width - columnWidth) / 2
        rightMargin: leftMargin

        add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationMedium } }
        remove: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationShort } }
        displaced: Transition { NumberAnimation { property: "y"; duration: Theme.durationMedium; easing.type: Easing.OutCubic } }

        header: Item {
            width: list.columnWidth
            height: 72
            Column {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -8
                Text { text: qsTr("Notifications"); color: Theme.surfaceFg; font.pixelSize: Theme.displaySmall }
                Text {
                    text: page.plugin.available ? qsTr("%n from %1", "", list.count).arg(page.deviceName) : ""
                    color: Theme.surfaceVariantFg
                    font.pixelSize: Theme.bodyMedium
                }
            }
            Button3 {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -8
                style: "tonal"
                iconName: "clear_all"
                text: qsTr("Dismiss all")
                visible: page.plugin.anyDismissable
                onClicked: page.plugin.dismissAll()
            }
        }

        delegate: Rectangle {
            id: card
            required property string notificationId
            required property string appName
            required property string title
            required property string text
            required property string icon
            required property bool dismissable
            required property bool repliable
            required property real arrived
            property bool replying: false

            width: list.columnWidth
            height: body.implicitHeight + 32
            radius: Theme.radiusL
            color: Theme.surfaceContainerLow

            HoverHandler { id: hover }

            Avatar {
                id: avatar
                x: 16; y: 16
                size: 40
                name: card.appName
                source: card.icon
                fallbackIcon: "notifications"
            }

            Column {
                id: body
                anchors.left: avatar.right
                anchors.leftMargin: 16
                anchors.right: tools.left
                anchors.rightMargin: 8
                y: 16
                spacing: 2

                Text {
                    width: parent.width
                    text: card.appName + (card.arrived > 0 ? " · " + Theme.shortStamp(card.arrived) : "")
                    color: Theme.surfaceVariantFg
                    font.pixelSize: Theme.labelMedium
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: card.title
                    color: Theme.surfaceFg
                    font.pixelSize: Theme.bodyLarge
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                Text {
                    id: bodyText
                    property bool expanded: false
                    width: parent.width
                    visible: card.text.length > 0
                    text: Theme.linkify(card.text, Theme.primary)
                    textFormat: Text.StyledText
                    color: Theme.surfaceVariantFg
                    font.pixelSize: Theme.bodyMedium
                    wrapMode: Text.Wrap
                    maximumLineCount: expanded ? 1000 : 4
                    elide: Text.ElideRight
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                    TapHandler { enabled: bodyText.truncated || bodyText.expanded; onTapped: bodyText.expanded = !bodyText.expanded }
                    HoverHandler { cursorShape: bodyText.hoveredLink ? Qt.PointingHandCursor : (bodyText.truncated ? Qt.PointingHandCursor : Qt.ArrowCursor) }
                }

                // Inline reply
                Item {
                    width: parent.width
                    height: card.replying ? 56 : 0
                    visible: card.replying
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: sendReply.left
                        anchors.rightMargin: 8
                        anchors.bottom: parent.bottom
                        height: 44
                        radius: 22
                        color: Theme.surfaceContainerHigh
                        B.TextField {
                            id: replyField
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            background: null
                            color: Theme.surfaceFg
                            placeholderText: qsTr("Reply to %1").arg(card.title || card.appName)
                            placeholderTextColor: Theme.surfaceVariantFg
                            selectionColor: Theme.alpha(Theme.primary, 0.35)
                            font.pixelSize: Theme.bodyMedium
                            onAccepted: sendReply.clicked()
                            Keys.onEscapePressed: card.replying = false
                        }
                    }
                    IconButton {
                        id: sendReply
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 2
                        style: "filled"
                        iconName: "send"
                        tooltip: qsTr("Send reply")
                        enabled: replyField.text.trim().length > 0
                        onClicked: {
                            page.plugin.reply(card.notificationId, replyField.text)
                            page.window.toast(qsTr("Reply sent"))
                            replyField.text = ""
                            card.replying = false
                        }
                    }
                }
            }

            Row {
                id: tools
                anchors.right: parent.right
                anchors.rightMargin: 8
                y: 12
                opacity: hover.hovered || card.replying ? 1 : 0.6
                IconButton {
                    iconName: "reply"
                    tooltip: qsTr("Reply")
                    visible: card.repliable
                    checked: card.replying
                    onClicked: { card.replying = !card.replying; if (card.replying) replyField.forceActiveFocus() }
                }
                IconButton {
                    iconName: "content_copy"
                    tooltip: qsTr("Copy text")
                    onClicked: { App.clipboard.copy(card.title ? card.title + "\n" + card.text : card.text); page.window.toast(qsTr("Copied")) }
                }
                IconButton {
                    iconName: "close"
                    tooltip: qsTr("Dismiss")
                    visible: card.dismissable
                    onClicked: page.plugin.dismiss(card.notificationId)
                }
            }
        }

        EmptyState {
            anchors.centerIn: parent
            visible: list.count === 0
            iconName: page.plugin.available ? "notifications" : "notifications_off"
            title: page.plugin.available ? qsTr("All caught up") : qsTr("Notifications unavailable")
            text: page.plugin.available ? qsTr("Notifications from %1 show up here. Dismissing one here also clears it on the phone.").arg(page.deviceName)
                                        : qsTr("Connect your phone, then in KDE Connect on the phone enable \"Notification sync\" for this computer and grant it notification access.")
        }
    }
}
