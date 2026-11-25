import QtQuick
import QtQuick.Layouts
import Slonnect

// Two-pane messaging: conversation list + open thread. Collapses to one pane
// on narrow windows.
Item {
    id: page
    property var window
    readonly property bool narrow: width < 860
    readonly property bool threadOpen: App.sms.threadOpen

    ConversationList {
        id: list
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: page.narrow ? parent.width : Math.min(400, Math.max(320, parent.width * 0.34))
        visible: !page.narrow || !page.threadOpen
        onNewConversation: picker.openForRecipients()
    }

    Rectangle {
        id: divider
        visible: !page.narrow
        anchors.left: list.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.outlineVariant
    }

    ThreadView {
        id: thread
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: page.narrow ? parent.left : divider.right
        anchors.right: parent.right
        visible: !page.narrow || page.threadOpen
        showBack: page.narrow
        onAttachContact: picker.openForCard()
        onToast: (text) => page.window.toast(text)
    }

    // Pick up text handed over on the command line once the page is showing.
    function takeDraft() {
        const text = App.takeDraft()
        if (text)
            thread.prefill(text)
    }
    Component.onCompleted: takeDraft()
    Connections {
        target: App
        function onShowMessages() { page.takeDraft() }
    }

    ContactPicker {
        id: picker
        parent: page.window ? page.window.contentItem : page
        onRecipientsChosen: (addresses) => App.sms.startConversation(addresses)
        onCardChosen: (path) => thread.addAttachment(path)
    }
}
