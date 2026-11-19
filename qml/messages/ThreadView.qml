pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

Rectangle {
    id: root
    property bool showBack: false
    signal attachContact()
    signal toast(string text)

    function addAttachment(path) { composer.addAttachment(path) }
    function prefill(text) { composer.prefill(text) }

    color: Theme.surface
    readonly property bool active: App.sms.currentThread >= 0 || App.sms.composing
    readonly property var addresses: App.sms.currentAddresses

    // --- header ---------------------------------------------------------------
    Rectangle {
        id: header
        visible: root.active
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 72
        color: Theme.surface
        z: 2

        IconButton {
            id: back
            visible: root.showBack
            iconName: "arrow_back"
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            onClicked: App.sms.close()
        }
        Avatar {
            id: headerAvatar
            anchors.left: root.showBack ? back.right : parent.left
            anchors.leftMargin: root.showBack ? 4 : 24
            anchors.verticalCenter: parent.verticalCenter
            size: 40
            name: App.sms.currentTitle
            source: root.addresses.length === 1 ? App.contacts.avatarFor(root.addresses[0]) : ""
            fallbackIcon: root.addresses.length > 1 ? "group" : "person"
        }
        Column {
            anchors.left: headerAvatar.right
            anchors.leftMargin: 16
            anchors.right: actions.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            Text {
                width: parent.width
                text: App.sms.currentTitle
                elide: Text.ElideRight
                color: Theme.surfaceFg
                font.pixelSize: Theme.titleMedium
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                visible: text.length > 0 && text !== App.sms.currentTitle
                text: root.addresses.length === 1 ? App.contacts.isKnown(root.addresses[0]) ? root.addresses[0] : ""
                                                 : qsTr("%n people", "", root.addresses.length)
                elide: Text.ElideRight
                color: Theme.surfaceVariantFg
                font.pixelSize: Theme.bodyMedium
            }
        }
        Row {
            id: actions
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            IconButton {
                iconName: "content_copy"
                tooltip: qsTr("Copy number")
                visible: root.addresses.length === 1
                onClicked: { App.clipboard.copy(root.addresses[0]); root.toast(qsTr("Copied %1").arg(root.addresses[0])) }
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.outlineVariant
            opacity: messages.atYEnd ? 0 : 1
        }
    }

    // --- messages -------------------------------------------------------------
    ListView {
        id: messages
        visible: root.active
        anchors.top: header.bottom
        anchors.bottom: composer.top
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        model: App.sms.messages
        // Newest at index 0, anchored to the bottom like every chat app.
        verticalLayoutDirection: ListView.BottomToTop
        reuseItems: true
        cacheBuffer: 1200
        spacing: 0
        topMargin: 12
        bottomMargin: 12
        boundsBehavior: Flickable.StopAtBounds
        B.ScrollBar.vertical: B.ScrollBar { policy: B.ScrollBar.AsNeeded }

        delegate: MessageBubble {
            width: messages.width
            onToast: (text) => root.toast(text)
        }

        // Fetch older history when scrolled near the top.
        onContentYChanged: maybeLoadOlder()
        onCountChanged: maybeLoadOlder()
        function maybeLoadOlder() {
            if (count > 0 && !App.sms.loadingMessages && !App.sms.reachedStart && contentHeight > 0
                    && (atYBeginning || contentY - originY < 800))
                App.sms.loadOlder()
        }

        // In a bottom-to-top view the footer sits above the oldest message.
        footer: Item {
            width: messages.width
            height: App.sms.loadingMessages ? 48 : 0
            B.BusyIndicator {
                anchors.centerIn: parent
                running: App.sms.loadingMessages
                visible: running
                width: 32; height: 32
            }
        }
    }

    EmptyState {
        anchors.centerIn: messages
        visible: root.active && messages.count === 0 && !App.sms.loadingMessages
        iconName: "chat_bubble"
        title: App.sms.composing ? qsTr("New conversation") : qsTr("No messages loaded")
        text: App.sms.composing ? qsTr("Say hello to %1").arg(App.sms.currentTitle) : ""
    }

    IconButton {
        anchors.right: parent.right
        anchors.bottom: composer.top
        anchors.margins: 16
        style: "tonal"
        iconName: "arrow_downward"
        onClicked: messages.positionViewAtBeginning()
        visible: root.active && messages.count > 0 && !messages.atYEnd
    }

    Composer {
        id: composer
        visible: root.active
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        enabled: App.sms.available
        onAttachContact: root.attachContact()
        onSent: messages.positionViewAtBeginning()
    }

    // Dropping files anywhere on the thread attaches them.
    DropArea {
        anchors.fill: parent
        enabled: root.active
        onDropped: (drop) => {
            for (const url of drop.urls)
                composer.addAttachment(url.toString())
            drop.accept(Qt.CopyAction)
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 12
            visible: parent.containsDrag
            radius: Theme.radiusXL
            color: Theme.alpha(Theme.primaryContainer, 0.85)
            border.width: 2
            border.color: Theme.primary
            Column {
                anchors.centerIn: parent
                spacing: 8
                Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "attach_file"; size: 48; color: Theme.primaryContainerFg }
                Text { text: qsTr("Drop to attach"); color: Theme.primaryContainerFg; font.pixelSize: Theme.titleMedium }
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        visible: !root.active
        iconName: "forum"
        title: qsTr("Your messages")
        text: qsTr("Pick a conversation, or start a new one. SMS, MMS and RCS from your phone all show up here.")
    }
}
