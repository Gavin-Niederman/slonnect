pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import Slonnect

// Searchable list of node types to add. Type to filter, Enter adds the first match.
B.Popup {
    id: root
    required property var colorsFor   // function(typeId) -> [bg, fg]
    signal chosen(string typeId)

    readonly property var types: App.actions.nodeTypes.filter(t => !t.trigger)
    readonly property var matches: {
        const q = search.text.trim().toLowerCase()
        if (!q)
            return types
        return types.filter(t => (t.title + " " + t.category + " " + t.description).toLowerCase().includes(q))
    }

    onMatchesChanged: {
        filtered.clear()
        for (const t of matches)
            filtered.append({ typeId: t.id, title: t.title, category: t.category, iconName: t.icon, description: t.description })
    }
    Component.onCompleted: matchesChanged()
    ListModel { id: filtered }

    width: 340
    height: Math.min(520, parent ? parent.height - 32 : 520)
    padding: 0
    focus: true
    onOpened: { search.text = ""; search.field.forceActiveFocus() }

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surfaceContainerHigh
        Rectangle {
            anchors.fill: parent; anchors.topMargin: 3; z: -1
            radius: parent.radius; color: Theme.alpha(Theme.scrim, Theme.dark ? 0.5 : 0.18)
        }
    }

    contentItem: Column {
        spacing: 0
        SearchBar {
            id: search
            width: parent.width - 16
            x: 8
            y: 8
            height: 44
            color: Theme.surfaceContainerHighest
            placeholder: qsTr("Add a node…")
            onAccepted: if (root.matches.length) { root.chosen(root.matches[0].id); root.close() }
        }
        Item { width: 1; height: 8 }
        ListView {
            id: list
            width: parent.width
            height: root.height - search.height - 24
            clip: true
            model: filtered
            boundsBehavior: Flickable.StopAtBounds
            B.ScrollBar.vertical: B.ScrollBar {}
            section.property: "category"
            section.delegate: SectionHeader {
                required property string section
                x: 16
                text: section
            }
            delegate: B.ItemDelegate {
                id: entry
                required property string typeId
                required property string title
                required property string iconName
                required property string description
                width: list.width
                height: 56
                hoverEnabled: true
                onClicked: { root.chosen(typeId); root.close() }
                background: Rectangle {
                    color: "transparent"
                    StateLayer { hovered: entry.hovered; pressed: entry.pressed }
                }
                contentItem: Item {
                    Rectangle {
                        id: badge
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 32; height: 32; radius: Theme.radiusS
                        color: root.colorsFor(entry.typeId)[0]
                        Icon {
                            anchors.centerIn: parent
                            name: entry.iconName
                            size: 18
                            color: root.colorsFor(entry.typeId)[1]
                        }
                    }
                    Column {
                        anchors.left: badge.right
                        anchors.leftMargin: 12
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            width: parent.width
                            text: entry.title
                            color: Theme.surfaceFg
                            font.pixelSize: Theme.bodyMedium
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: entry.description
                            color: Theme.surfaceVariantFg
                            font.pixelSize: Theme.labelMedium
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
