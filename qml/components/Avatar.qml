import QtQuick
import Slonnect

// Circular contact photo with tonal initials fallback.
Rectangle {
    id: root
    property string name
    property string source
    property string fallbackIcon: "person"
    property int size: 40

    width: size
    height: size
    radius: size / 2
    readonly property var colors: Theme.avatarColors(name || "?")
    color: colors[0]

    readonly property string letters: Theme.initials(name)
    readonly property bool hasLetters: Theme.hasLetter(letters)

    Text {
        anchors.centerIn: parent
        visible: !photo.visible && root.hasLetters
        text: root.letters
        color: root.colors[1]
        font.pixelSize: root.size * 0.4
        font.weight: Font.Medium
    }
    Icon {
        anchors.centerIn: parent
        visible: !photo.visible && !root.hasLetters
        name: root.fallbackIcon
        filled: true
        size: root.size * 0.6
        color: root.colors[1]
    }
    RoundedImage {
        id: photo
        anchors.fill: parent
        visible: status === Image.Ready
        source: root.source
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        radius: root.radius
    }
}
