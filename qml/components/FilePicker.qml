import QtQuick
import QtQuick.Dialogs
import Slonnect

// Opens the desktop's file chooser through the XDG portal; falls back to Qt's
// dialog only when no portal is running. Emits file:// URL strings.
Item {
    id: root
    property string title
    property bool multiple: true
    signal picked(var urls)

    function open() { portal.open() }

    PortalFileDialog {
        id: portal
        title: root.title
        multiple: root.multiple
        onAccepted: (urls) => root.picked(urls)
        onUnavailable: fallback.open()
    }
    FileDialog {
        id: fallback
        title: root.title
        fileMode: root.multiple ? FileDialog.OpenFiles : FileDialog.OpenFile
        onAccepted: root.picked(selectedFiles.map(u => u.toString()))
    }
}
