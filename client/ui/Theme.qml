pragma Singleton
import QtQuick

// Design tokens of the FrameBeam Player (docs/design/tokens.md, v4: Signal Cyan). Dark is the designed palette;
// the light palette is derived from the hub tokens (not designed for the Player) and carries the same names.
QtObject {
    id: root

    property bool dark: true

    // Accent: Signal Cyan. Also the warning color (intentionally the same value, see tokens.md "Status").
    readonly property color accent: dark ? "#3cbfd8" : "#1a7f96"
    readonly property color textOnAccent: dark ? "#161512" : "#ffffff"

    readonly property color bg: dark ? "#121315" : "#f6f5f2"
    readonly property color bgSidebar: dark ? "#0e0f10" : "#efeee9"
    readonly property color bgPanel: dark ? "#16171a" : "#faf9f7"
    readonly property color surface: dark ? "#1a1b1e" : "#ffffff"
    readonly property color surfaceRaised: dark ? "#1f2024" : "#ebeae5"
    readonly property color surfaceHub: dark ? "#17181b" : "#ffffff"
    readonly property color tile: dark ? "#1b1c1f" : "#ebeae5"
    readonly property color tileStripe: dark ? "#17181a" : "#e2e0da"

    // Game view is always dark.
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
    readonly property color warn: accent
    readonly property color warnBg: accentChipBg
    readonly property color error: dark ? "#ef8a78" : "#b5402f"
    readonly property color errorBg: dark ? "#2a1a17" : "#fbf1ef"
    readonly property color errorText: dark ? "#e6d6d2" : "#6b2a20"

    // ---- v4 Player tokens (0.6). The names of this block are shared with the in-game views: keep them identical.
    readonly property color accentChipBg: dark ? "#15262b" : "#ddf1f6"
    readonly property color accentBorder: dark ? "#1a3238" : "#a6d9e6"
    readonly property color infoBg: dark ? "#14222a" : "#eef8fa"
    readonly property color infoBorder: dark ? "#1a3238" : "#a6d9e6"
    readonly property color infoText: dark ? "#a9cdd6" : "#0f5a6b"
    readonly property color neutralPillBg: dark ? "#232428" : "#ebeae5"
    readonly property color inputError: dark ? "#c4544a" : "#b5402f"
    readonly property color popupBg: dark ? "#1d1e22" : "#ffffff"
    readonly property color borderPopup: dark ? "#2f3035" : "#d9d7d1"
    readonly property color textMeta: dark ? "#8e8e94" : "#6b6a66"
    readonly property color textTile: dark ? "#6f6f75" : "#8a8984"
    readonly property color overlayBg: Qt.rgba(17 / 255, 18 / 255, 20 / 255, 0.94)
    readonly property color toolbarBg: Qt.rgba(22 / 255, 23 / 255, 26 / 255, 0.92)
    // ---- end of the shared v4 block

    // Radii (tokens.md): 4, 5 (segment), 6, 7 (input, button), 8 (card, primary button), 9, 10 (Hub card), 11 (toggle), 12 (dialog),
    // 16 (filter chip).
    readonly property int radius4: 4
    readonly property int radius5: 5
    readonly property int radius6: 6
    readonly property int radius7: 7
    readonly property int radius8: 8
    readonly property int radius9: 9
    readonly property int radius10: 10
    readonly property int radius11: 11
    readonly property int radius12: 12
    readonly property int radius16: 16
    readonly property int radiusPill: 999

    // Spacing (gap values of the design, most frequent: 10, 4, 8).
    readonly property int space4: 4
    readonly property int space6: 6
    readonly property int space8: 8
    readonly property int space10: 10
    readonly property int space12: 12
    readonly property int space14: 14
    readonly property int space16: 16
    readonly property int space20: 20
    readonly property int space28: 28

    // Layout
    readonly property int sidebarWidth: 232
    readonly property int columnWidth: 300       // second column of 3e / 3f / 3p
    readonly property int detailWidth: 392       // Library detail column
    readonly property int inputTestWidth: 340    // Controllers input test

    // Font size roles
    readonly property int fontHero: 30      // start / pairing title
    readonly property int fontDialog: 24    // dialog title
    readonly property int fontPage: 26      // page title
    readonly property int fontDetail: 22    // detail title
    readonly property int fontCard: 16      // card title
    readonly property int fontSection: 15   // section / group title
    readonly property int fontBody: 14      // body, rows
    readonly property int fontSmall: 13     // controls, secondary rows
    readonly property int fontMeta: 12      // meta, help text, mono values
    readonly property int fontMono: 11      // eyebrow, slots, badges

    // Motion (short; never in the game view)
    readonly property int durFast: 120
    readonly property int durPage: 160
    readonly property int durList: 180

    // System fonts (Inter/Plex are not bundled).
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
        default: return neutralPillBg
        }
    }
}
