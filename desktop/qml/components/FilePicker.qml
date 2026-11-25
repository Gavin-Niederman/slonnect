import QtQuick
import QtQuick.Dialogs
import Slonnect

// Opens the desktop's file chooser through the XDG portal; falls back to Qt's
// dialog only when no portal is running. Emits file:// URL strings.
Item {
    id: root
    property string title
    property bool multiple: true
    property bool directory: false
    signal picked(var urls)

    function open() { portal.open() }

    PortalFileDialog {
        id: portal
        title: root.title
        multiple: root.multiple && !root.directory
        directory: root.directory
        onAccepted: (urls) => root.picked(urls)
        onUnavailable: root.directory ? folderFallback.open() : fallback.open()
    }
    FolderDialog {
        id: folderFallback
        title: root.title
        onAccepted: root.picked([selectedFolder.toString()])
    }
    FileDialog {
        id: fallback
        title: root.title
        fileMode: root.multiple ? FileDialog.OpenFiles : FileDialog.OpenFile
        onAccepted: root.picked(selectedFiles.map(u => u.toString()))
    }
}
