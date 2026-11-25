pragma Singleton
import QtQuick
import QtCore

// Material 3 colour roles, shape and type scale. Baseline scheme (seed #6750A4).
QtObject {
    id: theme

    // "system" | "light" | "dark"
    property string mode: settings.mode
    readonly property bool dark: mode === "dark" || (mode === "system" && Qt.styleHints.colorScheme === Qt.ColorScheme.Dark)

    property Settings settings: Settings {
        category: "appearance"
        property string mode: "system"
    }
    function setMode(m) { settings.mode = m }

    readonly property color primary: dark ? "#D0BCFF" : "#6750A4"
    readonly property color primaryFg: dark ? "#381E72" : "#FFFFFF"
    readonly property color primaryContainer: dark ? "#4F378B" : "#EADDFF"
    readonly property color primaryContainerFg: dark ? "#EADDFF" : "#21005D"
    readonly property color secondary: dark ? "#CCC2DC" : "#625B71"
    readonly property color secondaryContainer: dark ? "#4A4458" : "#E8DEF8"
    readonly property color secondaryContainerFg: dark ? "#E8DEF8" : "#1D192B"
    readonly property color tertiary: dark ? "#EFB8C8" : "#7D5260"
    readonly property color tertiaryContainer: dark ? "#633B48" : "#FFD8E4"
    readonly property color tertiaryContainerFg: dark ? "#FFD8E4" : "#31111D"
    readonly property color error: dark ? "#F2B8B5" : "#B3261E"
    readonly property color errorContainer: dark ? "#8C1D18" : "#F9DEDC"
    readonly property color errorContainerFg: dark ? "#F9DEDC" : "#410E0B"
    readonly property color surface: dark ? "#141218" : "#FEF7FF"
    readonly property color surfaceContainerLowest: dark ? "#0F0D13" : "#FFFFFF"
    readonly property color surfaceContainerLow: dark ? "#1D1B20" : "#F7F2FA"
    readonly property color surfaceContainer: dark ? "#211F26" : "#F3EDF7"
    readonly property color surfaceContainerHigh: dark ? "#2B2930" : "#ECE6F0"
    readonly property color surfaceContainerHighest: dark ? "#36343B" : "#E6E0E9"
    readonly property color surfaceFg: dark ? "#E6E0E9" : "#1D1B20"
    readonly property color surfaceVariantFg: dark ? "#CAC4D0" : "#49454F"
    readonly property color outline: dark ? "#938F99" : "#79747E"
    readonly property color outlineVariant: dark ? "#49454F" : "#CAC4D0"
    readonly property color inverseSurface: dark ? "#E6E0E9" : "#322F35"
    readonly property color inverseSurfaceFg: dark ? "#322F35" : "#F5EFF7"
    readonly property color inversePrimary: dark ? "#6750A4" : "#D0BCFF"
    readonly property color scrim: "#000000"

    // State layer opacities
    readonly property real hoverOpacity: 0.08
    readonly property real pressOpacity: 0.12

    // Shape
    readonly property int radiusXS: 4
    readonly property int radiusS: 8
    readonly property int radiusM: 12
    readonly property int radiusL: 16
    readonly property int radiusXL: 28

    // Motion
    readonly property int durationShort: 150
    readonly property int durationMedium: 250
    readonly property int durationLong: 400

    // Type scale (px)
    readonly property int displaySmall: 36
    readonly property int headlineSmall: 24
    readonly property int titleLarge: 22
    readonly property int titleMedium: 16
    readonly property int bodyLarge: 16
    readonly property int bodyMedium: 14
    readonly property int labelLarge: 14
    readonly property int labelMedium: 12
    readonly property int labelSmall: 11

    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
    function mix(a, b, t) { return Qt.rgba(a.r * (1 - t) + b.r * t, a.g * (1 - t) + b.g * t, a.b * (1 - t) + b.b * t, 1) }

    // Tonal avatar colours, picked by a stable hash of the name.
    readonly property var avatarPalette: dark
        ? [["#4F378B", "#EADDFF"], ["#633B48", "#FFD8E4"], ["#4A4458", "#E8DEF8"], ["#00504A", "#9EF2E4"], ["#5B4300", "#FFDF9C"], ["#174A7E", "#D3E3FF"]]
        : [["#EADDFF", "#21005D"], ["#FFD8E4", "#31111D"], ["#E8DEF8", "#1D192B"], ["#9EF2E4", "#00201D"], ["#FFDF9C", "#251A00"], ["#D3E3FF", "#001C38"]]
    function avatarColors(key) {
        let h = 0
        for (let i = 0; i < key.length; ++i)
            h = (h * 31 + key.charCodeAt(i)) | 0
        return avatarPalette[Math.abs(h) % avatarPalette.length]
    }

    // --- formatting helpers --------------------------------------------------
    function isSameDay(a, b) {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate()
    }
    function timeOfDay(ms) {
        return new Date(ms).toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
    }
    // Compact timestamp for list rows.
    function shortStamp(ms) {
        const d = new Date(ms), now = new Date()
        if (isSameDay(d, now))
            return timeOfDay(ms)
        const days = Math.floor((now - d) / 86400000)
        if (days < 6)
            return d.toLocaleDateString(Qt.locale(), "ddd")
        if (d.getFullYear() === now.getFullYear())
            return d.toLocaleDateString(Qt.locale(), "MMM d")
        return d.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }
    function dayLabel(ms) {
        const d = new Date(ms), now = new Date()
        if (isSameDay(d, now))
            return qsTr("Today")
        const y = new Date(now.getTime() - 86400000)
        if (isSameDay(d, y))
            return qsTr("Yesterday")
        if (d.getFullYear() === now.getFullYear())
            return d.toLocaleDateString(Qt.locale(), "dddd, MMMM d")
        return d.toLocaleDateString(Qt.locale(), "MMMM d, yyyy")
    }
    function duration(ms) {
        const s = Math.max(0, Math.floor(ms / 1000))
        const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60
        const pad = n => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + pad(m) + ":" + pad(sec) : m + ":" + pad(sec)
    }
    function isLetter(ch) { return ch.toLowerCase() !== ch.toUpperCase() || ch.charCodeAt(0) > 0x2E7F && ch.charCodeAt(0) < 0xD800 }
    function hasLetter(s) { return Array.from(s || "").some(isLetter) }
    function initials(name) {
        // Keep letters and digits. (Qt's JS engine has no \p{…} regex escapes; a character
        // with distinct upper and lower case is a letter, which covers most scripts.)
        const keep = ch => isLetter(ch) || (ch >= "0" && ch <= "9") || ch === " "
        const parts = Array.from(name || "").filter(keep).join("").trim().split(/\s+/)
        if (!parts.length || !parts[0])
            return ""
        return (parts[0][0] + (parts.length > 1 ? parts[parts.length - 1][0] : "")).toUpperCase()
    }
    function escapeHtml(s) {
        return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;")
    }
    readonly property var urlPattern: /(https?:\/\/[^\s<]+[^\s<.,:;"')\]!?])/g
    // Plain text -> StyledText with clickable links.
    function linkify(s, linkColor) {
        const esc = escapeHtml(s).replace(/\n/g, "<br>")
        if (esc.indexOf("http") < 0)
            return esc
        return esc.replace(urlPattern, '<a href="$1" style="color:' + linkColor + '">$1</a>')
    }
}
