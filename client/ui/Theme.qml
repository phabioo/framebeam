pragma Singleton
import QtQuick

// Design-Tokens des FrameBeam Player (docs/design/tokens.md). Dunkel ist die entworfene Palette; die
// helle Palette ist aus den Hub-Tokens abgeleitet (fuer den Player nicht entworfen) und gleich benannt.
QtObject {
    id: root

    property bool dark: true

    readonly property color accent: "#e9b44c"
    readonly property color textOnAccent: "#161512"

    readonly property color bg: dark ? "#121315" : "#f6f5f2"
    readonly property color bgSidebar: dark ? "#0e0f10" : "#efeee9"
    readonly property color bgPanel: dark ? "#16171a" : "#faf9f7"
    readonly property color surface: dark ? "#1a1b1e" : "#ffffff"
    readonly property color surfaceRaised: dark ? "#1f2024" : "#ebeae5"
    readonly property color surfaceHub: dark ? "#17181b" : "#ffffff"
    readonly property color tile: dark ? "#1b1c1f" : "#ebeae5"
    readonly property color tileStripe: dark ? "#17181a" : "#e2e0da"

    // Spielansicht ist immer dunkel.
    readonly property color gameBg: "#0b0b0c"
    readonly property color gameHeader: "#111214"
    readonly property color gameBorder: "#1e1f22"
    readonly property color gameText: "#ecebe7"
    readonly property color gameTextMuted: "#a3a3a8"

    readonly property color borderSidebar: dark ? "#232428" : "#e3e1dc"
    readonly property color borderCard: dark ? "#26272b" : "#e3e1dc"
    readonly property color borderInput: dark ? "#2c2d32" : "#d9d7d1"
    readonly property color borderButton: dark ? "#3a3b40" : "#d9d7d1"
    readonly property color borderRow: dark ? "#1f2024" : "#efeee9"

    readonly property color text: dark ? "#ecebe7" : "#1b1b1d"
    readonly property color textSecondary: dark ? "#c9c8c4" : "#55544f"
    readonly property color textMuted: dark ? "#a3a3a8" : "#6b6a66"
    readonly property color textFaint: dark ? "#7d7d83" : "#8a8984"
    readonly property color textDisabled: dark ? "#5e5e63" : "#a9a8a2"
    readonly property color monogram: dark ? "#45464c" : "#b9b7b0"

    readonly property color ok: dark ? "#6fd39a" : "#1f6a3f"
    readonly property color okBg: dark ? "#17291f" : "#e2f1e7"
    readonly property color warn: dark ? "#e9b44c" : "#9a6a12"
    readonly property color warnBg: dark ? "#2a2418" : "#f6e6c4"
    readonly property color error: dark ? "#ef8a78" : "#b5402f"
    readonly property color errorBg: dark ? "#2a1a17" : "#fbf1ef"
    readonly property color errorText: dark ? "#e6d6d2" : "#6b2a20"

    // Systemschriften (Inter/Plex werden nicht mitgeliefert).
    readonly property string mono: Qt.platform.os === "windows" ? "Consolas" : (Qt.platform.os === "osx" ? "Menlo" : "monospace")

    function toneColor(tone: string): color {
        switch (tone) {
        case "ok": return ok
        case "warn": return warn
        case "error": return error
        default: return textMuted
        }
    }
    function toneBg(tone: string): color {
        switch (tone) {
        case "ok": return okBg
        case "warn": return warnBg
        case "error": return errorBg
        default: return surfaceRaised
        }
    }
}
