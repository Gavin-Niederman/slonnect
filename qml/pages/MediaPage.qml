pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// Now-playing remote for the phone's media sessions, tinted from the cover art.
Rectangle {
    id: page
    property var window
    readonly property var media: App.media
    readonly property color seed: media.artColor.valid ? media.artColor : Theme.primary
    // Tonal surfaces derived from the cover colour.
    readonly property color tintSurface: Theme.mix(Theme.surface, seed, Theme.dark ? 0.16 : 0.10)
    readonly property color tintContainer: Theme.mix(Theme.surface, seed, Theme.dark ? 0.32 : 0.22)
    readonly property color accent: Theme.dark ? Qt.lighter(seed, 1.5) : Qt.darker(seed, 1.25)

    color: tintSurface
    Behavior on color { ColorAnimation { duration: Theme.durationLong } }

    // Extrapolated playback position; ticks only while visible and playing.
    property real now: Date.now()
    Timer {
        interval: 500
        repeat: true
        running: page.visible && page.media.playing && !seek.pressed
        onTriggered: page.now = Date.now()
    }
    readonly property real position: Math.min(media.length,
        media.position + (media.playing ? Math.max(0, now - media.positionTimestamp) : 0))

    Keys.onSpacePressed: media.playPause()
    Keys.onLeftPressed: media.seekBy(-10000)
    Keys.onRightPressed: media.seekBy(10000)
    onVisibleChanged: if (visible) forceActiveFocus()

    EmptyState {
        anchors.centerIn: parent
        visible: !page.media.available || !page.media.hasMedia
        iconName: "music_off"
        title: !page.media.available ? qsTr("Media control unavailable") : qsTr("Nothing playing")
        text: !page.media.available ? qsTr("Connect your phone and enable Multimedia control in KDE Connect.")
                                    : qsTr("Start playing something on your phone and it will appear here.")
    }

    Item {
        anchors.fill: parent
        visible: page.media.available && page.media.hasMedia

        // Player selector
        Flow {
            id: players
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 24
            spacing: 8
            visible: page.media.players.length > 1
            Repeater {
                model: page.media.players
                delegate: Chip {
                    required property string modelData
                    text: modelData
                    checkable: true
                    checked: modelData === page.media.player
                    onClicked: page.media.player = modelData
                }
            }
        }

        Column {
            id: main
            anchors.centerIn: parent
            anchors.verticalCenterOffset: players.visible ? 16 : 0
            width: Math.min(parent.width - 64, 440)
            spacing: 20

            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                readonly property real side: Math.max(120, Math.min(main.width, page.height - 420))
                width: side
                height: side
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusXL
                    color: page.tintContainer
                    visible: art.status !== Image.Ready
                    Icon { anchors.centerIn: parent; name: "music_note"; size: parent.width * 0.35; color: page.accent }
                }
                RoundedImage {
                    id: art
                    anchors.fill: parent
                    radius: Theme.radiusXL
                    source: page.media.albumArt
                    sourceSize: Qt.size(512, 512)
                }
            }

            Column {
                width: parent.width
                spacing: 4
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: page.media.title || qsTr("Unknown title")
                    elide: Text.ElideRight
                    color: Theme.surfaceFg
                    font.pixelSize: Theme.headlineSmall
                    font.weight: Font.Medium
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: [page.media.artist, page.media.album].filter(s => s).join(" — ")
                    elide: Text.ElideRight
                    color: Theme.surfaceVariantFg
                    font.pixelSize: Theme.bodyLarge
                }
            }

            Column {
                width: parent.width
                visible: page.media.length > 0
                B.Slider {
                    id: seek
                    width: parent.width
                    from: 0
                    to: Math.max(1, page.media.length)
                    enabled: page.media.canSeek
                    value: pressed ? value : page.position
                    onPressedChanged: if (!pressed) page.media.seekTo(value)
                    background: Rectangle {
                        x: seek.leftPadding
                        y: seek.topPadding + seek.availableHeight / 2 - height / 2
                        width: seek.availableWidth
                        height: 4
                        radius: 2
                        color: Theme.alpha(page.accent, 0.25)
                        Rectangle {
                            width: seek.visualPosition * parent.width
                            height: parent.height
                            radius: 2
                            color: page.accent
                        }
                    }
                    handle: Rectangle {
                        x: seek.leftPadding + seek.visualPosition * (seek.availableWidth - width)
                        y: seek.topPadding + seek.availableHeight / 2 - height / 2
                        width: 4
                        height: seek.pressed ? 28 : 20
                        radius: 2
                        color: page.accent
                        Behavior on height { NumberAnimation { duration: Theme.durationShort } }
                    }
                }
                Item {
                    width: parent.width
                    height: 16
                    Text { text: Theme.duration(seek.pressed ? seek.value : page.position); color: Theme.surfaceVariantFg; font.pixelSize: Theme.labelMedium }
                    Text { anchors.right: parent.right; text: Theme.duration(page.media.length); color: Theme.surfaceVariantFg; font.pixelSize: Theme.labelMedium }
                }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 16
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "replay_10"; tooltip: qsTr("Back 10 s")
                    visible: page.media.canSeek
                    onClicked: page.media.seekBy(-10000)
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 56; height: 56; iconSize: 32
                    iconName: "skip_previous"; tooltip: qsTr("Previous")
                    iconColor: Theme.surfaceFg
                    onClicked: page.media.previous()
                }
                B.AbstractButton {
                    id: play
                    anchors.verticalCenter: parent.verticalCenter
                    width: 96; height: 72
                    hoverEnabled: true
                    onClicked: page.media.playPause()
                    background: Rectangle {
                        // M3 expressive: the play button morphs between pill and rounded square.
                        radius: page.media.playing ? Theme.radiusL : height / 2
                        color: page.accent
                        Behavior on radius { NumberAnimation { duration: Theme.durationMedium; easing.type: Easing.OutBack } }
                        StateLayer { hovered: play.hovered; pressed: play.pressed; tint: page.tintSurface }
                    }
                    contentItem: Icon {
                        name: page.media.playing ? "pause" : "play_arrow"
                        filled: true
                        size: 40
                        color: page.tintSurface
                    }
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 56; height: 56; iconSize: 32
                    iconName: "skip_next"; tooltip: qsTr("Next")
                    iconColor: Theme.surfaceFg
                    onClicked: page.media.next()
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "forward_10"; tooltip: qsTr("Forward 10 s")
                    visible: page.media.canSeek
                    onClicked: page.media.seekBy(10000)
                }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 8
                width: Math.min(parent.width, 320)
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: page.media.volume === 0 ? "volume_off" : page.media.volume < 50 ? "volume_down" : "volume_up"
                    onClicked: page.media.volume = page.media.volume > 0 ? 0 : 50
                }
                B.Slider {
                    id: vol
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 48
                    from: 0; to: 100; stepSize: 1
                    value: page.media.volume
                    onMoved: page.media.volume = value
                    background: Rectangle {
                        x: vol.leftPadding
                        y: vol.topPadding + vol.availableHeight / 2 - height / 2
                        width: vol.availableWidth; height: 4; radius: 2
                        color: Theme.alpha(Theme.surfaceVariantFg, 0.25)
                        Rectangle { width: vol.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.surfaceVariantFg }
                    }
                    handle: Rectangle {
                        x: vol.leftPadding + vol.visualPosition * (vol.availableWidth - width)
                        y: vol.topPadding + vol.availableHeight / 2 - height / 2
                        width: 16; height: 16; radius: 8
                        color: Theme.surfaceVariantFg
                    }
                }
            }
        }

        Text {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 16
            text: qsTr("Playing on %1 · %2").arg(App.device ? App.device.name : "").arg(page.media.player)
                  + "   ·   " + qsTr("Space play/pause, ←/→ seek")
            color: Theme.surfaceVariantFg
            font.pixelSize: Theme.labelMedium
        }
    }
}
