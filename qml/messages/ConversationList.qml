pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

Rectangle {
    id: root
    signal newConversation()
    color: Theme.surface

    SearchBar {
        id: search
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 16
        placeholder: qsTr("Search conversations")
        onTextChanged: App.sms.conversations.search = text
        onAccepted: if (view.count > 0) view.itemAtIndex(0).open()
    }

    ListView {
        id: view
        anchors.top: search.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        model: App.sms.conversations
        reuseItems: true
        cacheBuffer: 600
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: 88
        B.ScrollBar.vertical: B.ScrollBar { policy: B.ScrollBar.AsNeeded }

        delegate: ConversationDelegate {
            width: view.width
            selected: threadId === App.sms.currentThread
        }

        // Pull fresh threads from the phone whenever the list is shown.
        Component.onCompleted: App.sms.refresh()
    }

    EmptyState {
        anchors.centerIn: view
        visible: view.count === 0
        iconName: App.sms.available ? (search.text ? "search_off" : "forum") : "sms_failed"
        title: !App.sms.available ? qsTr("Messaging unavailable")
             : search.text ? qsTr("No matches")
             : App.sms.loadingConversations ? qsTr("Loading conversations…") : qsTr("No conversations yet")
        text: !App.sms.available ? qsTr("Connect your phone and enable the SMS plugin in KDE Connect.") : ""
    }

    Fab {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        iconName: "edit"
        text: qsTr("Start chat")
        expanded: view.atYBeginning || view.contentY < 40
        enabled: App.sms.available
        onClicked: root.newConversation()
    }
}
