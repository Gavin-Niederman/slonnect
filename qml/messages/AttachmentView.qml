import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// Renders one MMS/RCS part: image/video thumbnail, contact card, or file chip.
Item {
    id: root
    required property var modelData
    property real maxWidth: 320
    property bool outgoing: false
    signal toast(string text)

    readonly property string kind: modelData.kind
    readonly property bool visual: kind === "image" || kind === "video"
    readonly property var contact: modelData.contact || ({})

    function open() {
        if (modelData.localPath)
            Qt.openUrlExternally("file://" + modelData.localPath)
        else if (modelData.id)
            App.sms.openAttachment(modelData.id)
    }

    width: visual ? thumbSize.width : kind === "vcard" ? Math.min(280, maxWidth) : Math.min(fileRow.implicitWidth + 24, maxWidth)
    height: visual ? thumbSize.height : kind === "vcard" ? card.implicitHeight : 56

    // Size thumbnails by their aspect ratio within a 260px box.
    QtObject {
        id: thumbSize
        readonly property real iw: thumb.implicitImageWidth > 0 ? thumb.implicitImageWidth : 4
        readonly property real ih: thumb.implicitImageHeight > 0 ? thumb.implicitImageHeight : 3
        readonly property real box: Math.min(260, root.maxWidth)
        readonly property real scale: Math.min(box / iw, box / ih)
        readonly property real width: Math.max(120, iw * scale)
        readonly property real height: Math.max(80, ih * scale)
    }

    Rectangle {
        anchors.fill: parent
        visible: root.visual && thumb.status !== Image.Ready
        radius: 16
        color: Theme.surfaceContainerHighest
        Icon { anchors.centerIn: parent; name: root.kind === "video" ? "movie" : "image"; size: 32 }
    }
    RoundedImage {
        id: thumb
        visible: root.visual
        anchors.fill: parent
        radius: 16
        source: root.visual ? root.modelData.thumbnail : ""
        fillMode: Image.PreserveAspectCrop
    }
    Rectangle {
        visible: root.kind === "video"
        anchors.centerIn: parent
        width: 48; height: 48; radius: 24
        color: Theme.alpha(Theme.scrim, 0.5)
        Icon { anchors.centerIn: parent; name: "play_arrow"; filled: true; color: "white" }
    }
    TapHandler {
        enabled: root.visual || root.kind === "file" || root.kind === "audio"
        onTapped: root.open()
    }
    HoverHandler {
        enabled: root.visual || root.kind === "file" || root.kind === "audio"
        cursorShape: Qt.PointingHandCursor
    }

    // Generic file / audio chip
    Rectangle {
        visible: root.kind === "file" || root.kind === "audio"
        anchors.fill: parent
        radius: 16
        color: root.outgoing ? Theme.primaryContainer : Theme.surfaceContainerHighest
        Row {
            id: fileRow
            anchors.verticalCenter: parent.verticalCenter
            x: 12
            spacing: 12
            Icon { name: root.kind === "audio" ? "audio_file" : "insert_drive_file"; anchors.verticalCenter: parent.verticalCenter; color: Theme.primary }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.modelData.name || qsTr("Attachment")
                width: Math.min(implicitWidth, root.maxWidth - 72)
                elide: Text.ElideMiddle
                color: Theme.surfaceFg
                font.pixelSize: Theme.bodyMedium
            }
        }
    }

    // Shared contact card (vCard)
    Rectangle {
        id: card
        visible: root.kind === "vcard"
        width: parent.width
        implicitHeight: cardColumn.implicitHeight + 24
        radius: 16
        color: root.outgoing ? Theme.primaryContainer : Theme.surfaceContainerHighest
        border.width: 1
        border.color: Theme.outlineVariant

        Column {
            id: cardColumn
            x: 12; y: 12
            width: parent.width - 24
            spacing: 8
            Row {
                spacing: 12
                width: parent.width
                Avatar {
                    size: 40
                    name: root.contact.name || ""
                    source: root.contact.photo || ""
                }
                Column {
                    width: parent.width - 52
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        width: parent.width
                        text: root.contact.name || qsTr("Contact card")
                        elide: Text.ElideRight
                        color: Theme.surfaceFg
                        font.pixelSize: Theme.titleMedium
                        font.weight: Font.Medium
                    }
                    Text {
                        width: parent.width
                        visible: text.length > 0
                        text: (root.contact.numbers || []).join(", ") || (root.contact.emails || []).join(", ")
                        elide: Text.ElideRight
                        color: Theme.surfaceVariantFg
                        font.pixelSize: Theme.bodyMedium
                    }
                }
            }
            Row {
                spacing: 4
                visible: (root.contact.numbers || []).length > 0 || !!root.modelData.localPath
                Button3 {
                    style: "text"
                    iconName: "chat"
                    text: qsTr("Message")
                    visible: (root.contact.numbers || []).length > 0
                    onClicked: App.sms.startConversation([root.contact.numbers[0]])
                }
                Button3 {
                    style: "text"
                    iconName: "content_copy"
                    text: qsTr("Copy")
                    visible: (root.contact.numbers || []).length > 0
                    onClicked: { App.clipboard.copy(root.contact.numbers[0]); root.toast(qsTr("Copied %1").arg(root.contact.numbers[0])) }
                }
                Button3 {
                    style: "text"
                    iconName: "person_add"
                    text: qsTr("Save")
                    visible: !!root.modelData.localPath
                    onClicked: root.open()
                }
            }
        }
    }
}
