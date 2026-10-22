pragma Singleton
import QtQuick

// Design tokens of the Uzume design system (docs/design/uzume/design-system.md, source of truth
// docs/design/source/uzume/ds/uzume/tokens.json): direction Ma, themes Sumi (dark) and Kinari (light), IBM Plex.
// New code uses the token names of the first block ("Design system tokens"). The legacy names below it (bg, text*, border*,
// surface*, accent*, warn*, error*, game*, radius*, font*) are kept so every existing screen picks up the palette now;
// they are remapped onto the tokens and are removed screen by screen in package 3b.
QtObject {
    id: root

    property bool dark: true

    // ---- Design system tokens --------------------------------------------------------------------------------------
    // Both palettes as tables (tokens.json). The game view stays Sumi in both themes and the plugin API passes `dark`
    // explicitly, so the tables are also read through tok().
    readonly property var sumi: ({
        ground: "#1c1b19", surface: "#242320", scrim: "#cc1c1b19", stage: "#171614", placeholder: "#262521", placeholderRule: "#34322e",
        ink: "#efece4", ink2: "#d3cfc5", muted: "#a19d94", dim: "#8a867c", rule: "#3a3833", rule2: "#2b2a27",
        hover: "#242320", pressed: "#2c2b28", selected: "#1af8b500",
        disabledGround: "#262521", disabledInk: "#6b6861", disabledRule: "#2b2a27",
        accent: "#f8b500", accentHover: "#ffc42e", accentPressed: "#dba000", accentText: "#f8b500", onAccent: "#1c1b19",
        danger: "#ee836f", dangerTint: "#1aee836f", ok: "#68be8d"
    })
    readonly property var kinari: ({
        ground: "#fbfaf5", surface: "#ffffff", scrim: "#ccfbfaf5", stage: "#f3f1e9", placeholder: "#e9e6dc", placeholderRule: "#d6d2c6",
        ink: "#1c1b19", ink2: "#3a3936", muted: "#5f5c55", dim: "#74706a", rule: "#d6d2c6", rule2: "#e9e6dd",
        hover: "#f4f2ea", pressed: "#ebe8de", selected: "#29f8b500",
        disabledGround: "#efede6", disabledInk: "#a3a097", disabledRule: "#e9e6dd",
        accent: "#f8b500", accentHover: "#ffc42e", accentPressed: "#e0a400", accentText: "#715c1f", onAccent: "#1c1b19",
        danger: "#b94047", dangerTint: "#12b94047", ok: "#007b43"
    })
    function withAlpha(c: color, a: real): color { return Qt.rgba(c.r, c.g, c.b, a) }
    function tok(name: string, isDark: bool): color { return (isDark ? sumi : kinari)[name] }

    // Planes
    readonly property color ground: dark ? sumi.ground : kinari.ground
    readonly property color surface: dark ? sumi.surface : kinari.surface
    readonly property color scrim: dark ? sumi.scrim : kinari.scrim
    readonly property color stage: dark ? sumi.stage : kinari.stage
    readonly property color placeholder: dark ? sumi.placeholder : kinari.placeholder
    readonly property color placeholderRule: dark ? sumi.placeholderRule : kinari.placeholderRule
    // Text and lines
    readonly property color ink: dark ? sumi.ink : kinari.ink
    readonly property color ink2: dark ? sumi.ink2 : kinari.ink2
    readonly property color muted: dark ? sumi.muted : kinari.muted
    readonly property color dim: dark ? sumi.dim : kinari.dim
    readonly property color rule: dark ? sumi.rule : kinari.rule
    readonly property color rule2: dark ? sumi.rule2 : kinari.rule2
    // States
    readonly property color hover: dark ? sumi.hover : kinari.hover
    readonly property color pressed: dark ? sumi.pressed : kinari.pressed
    readonly property color selected: dark ? sumi.selected : kinari.selected
    readonly property color disabledGround: dark ? sumi.disabledGround : kinari.disabledGround
    readonly property color disabledInk: dark ? sumi.disabledInk : kinari.disabledInk
    readonly property color disabledRule: dark ? sumi.disabledRule : kinari.disabledRule
    // Accent (Yamabuki)
    readonly property color accent: sumi.accent   // same in both themes
    readonly property color accentHover: sumi.accentHover
    readonly property color accentPressed: dark ? sumi.accentPressed : kinari.accentPressed
    readonly property color accentText: dark ? sumi.accentText : kinari.accentText
    readonly property color inkOnAccent: sumi.onAccent
    // Status
    readonly property color danger: dark ? sumi.danger : kinari.danger
    readonly property color dangerTint: dark ? sumi.dangerTint : kinari.dangerTint
    readonly property color ok: dark ? sumi.ok : kinari.ok
    // People: the four designed colours (join order Max, Lena, Jonas, Sam) are the same in both themes.
    readonly property color personWasurenagusa: "#89c3eb"
    readonly property color personMomo: "#f09199"
    readonly property color personMoegi: "#aacf53"
    readonly property color personFuji: "#bbbcde"
    readonly property color onPerson: "#1b1b1b"
    // Entries 5-8 are derived to the same rules (pastel, lightness 70-80 %, not Yamabuki, not the danger red, on-person
    // text >= 7:1): seiji 青磁, usu-fuji 薄藤, peach, nezumi-kinari. Not part of tokens.json.
    readonly property var playerColors: [personWasurenagusa, personMomo, personMoegi, personFuji, "#80d4c0", "#c8a3dd", "#e9ae8c", "#c9c3a6"]
    function playerColor(i: int, isDark: bool): color {
        const n = Math.max(1, Math.min(8, Math.round(i) || 1))
        return playerColors[n - 1]
    }

    // Families. The fonts are bundled (client/ui/fonts, loaded by uzume::ui::loadDesignFonts()); the old system mono font
    // is only the fallback when loading failed.
    readonly property bool fontsLoaded: Qt.fontFamilies().indexOf("IBM Plex Mono") >= 0
    readonly property string sans: "IBM Plex Sans"
    readonly property string mono: fontsLoaded ? "IBM Plex Mono"
                                                : (Qt.platform.os === "windows" ? "Consolas" : (Qt.platform.os === "osx" ? "Menlo" : "monospace"))
    readonly property string jp: "IBM Plex Sans JP"

    // Typography roles (design-system.md "Ma roles", "App sizes", "Brand"). size / line in px, weight as Font.*, spacing in px
    // (the em tracking of the design times the size, QML letterSpacing is absolute), family sans or mono, upper = uppercase.
    // Use: font.pixelSize: Theme.roles.body.size; font.weight: Theme.roles.body.weight; font.letterSpacing: Theme.roles.body.spacing.
    readonly property var roles: ({
        title:         { family: sans, size: 40, line: 40, weight: Font.Light,    spacing: -0.8,  upper: false },
        sectionLabel:  { family: mono, size: 10, line: 16, weight: Font.Medium,   spacing: 1.4,   upper: true },
        body:          { family: sans, size: 14, line: 22, weight: Font.Normal,   spacing: 0,     upper: false },
        meta:          { family: mono, size: 10, line: 16, weight: Font.Normal,   spacing: 0.6,   upper: true },
        meta11:        { family: mono, size: 11, line: 16, weight: Font.Normal,   spacing: 0,     upper: false },
        dialogTitle:   { family: sans, size: 20, line: 28, weight: Font.Normal,   spacing: -0.2,  upper: false },
        headerTitle:   { family: sans, size: 16, line: 24, weight: Font.Medium,   spacing: 0,     upper: false },
        itemTitle:     { family: sans, size: 14, line: 22, weight: Font.Medium,   spacing: 0,     upper: false },
        itemTitleSm:   { family: sans, size: 13, line: 18, weight: Font.Medium,   spacing: 0,     upper: false },
        button:        { family: sans, size: 14, line: 20, weight: Font.Medium,   spacing: 0,     upper: false },
        buttonSm:      { family: sans, size: 13, line: 20, weight: Font.Normal,   spacing: 0,     upper: false },
        input:         { family: sans, size: 13, line: 20, weight: Font.Normal,   spacing: 0,     upper: false },
        fieldLabel:    { family: sans, size: 12, line: 16, weight: Font.Medium,   spacing: 0,     upper: false },
        small:         { family: sans, size: 12, line: 18, weight: Font.Normal,   spacing: 0,     upper: false },
        tableHeader:   { family: mono, size: 10, line: 16, weight: Font.Medium,   spacing: 1.2,   upper: true },
        systemCode:    { family: mono, size: 10, line: 14, weight: Font.Medium,   spacing: 0.6,   upper: true },
        tag:           { family: mono, size: 10, line: 16, weight: Font.DemiBold, spacing: 1.0,   upper: true },
        address:       { family: mono, size: 11, line: 16, weight: Font.Normal,   spacing: 0,     upper: false },
        inviteCode:    { family: mono, size: 28, line: 36, weight: Font.Medium,   spacing: 3.92,  upper: true },
        verticalLabel: { family: mono, size: 10, line: 16, weight: Font.Normal,   spacing: 2.4,   upper: true },
        wordmarkNav:   { family: sans, size: 17, line: 20, weight: Font.DemiBold, spacing: -0.17, upper: false },
        wordmarkDisplay: { family: sans, size: 46, line: 46, weight: Font.DemiBold, spacing: -1.61, upper: false },
        jpAccent:      { family: jp,   size: 15, line: 20, weight: Font.Normal,   spacing: 0,     upper: false }
    })

    // Spacing (design-system.md "Spacing")
    readonly property int space05: 4
    readonly property int space1: 8
    readonly property int space15: 12
    readonly property int space2: 16
    readonly property int space3: 24
    readonly property int space4: 32
    readonly property int space5: 40
    readonly property int space7: 56

    // Sizes (design-system.md "Sizes")
    readonly property int row28: 28
    readonly property int row32: 32
    readonly property int row36: 36
    readonly property int row44: 44
    readonly property int row48: 48
    readonly property int header56: 56
    readonly property int sidebar: 232
    readonly property int openColumn: 152
    readonly property int sidePanel: 320
    readonly property int dialog: 400
    readonly property int dialogLg: 560
    readonly property int registerMark: 12
    readonly property int personMarker: 40
    readonly property int markNav: 20
    readonly property int menu: 200

    // Radius: none. The app icon tile (22.5 %) is the only rounded shape and is not drawn by QML.
    readonly property int radiusNone: 0

    // Focus ring (token focus-ring / focus-ring-inset): Sumi 2 px gap + 2 px Yamabuki, Kinari 2 px Yamabuki + 1 px Sumi outside.
    // Drawn by UzFocusRing. Widths in px, measured from the element edge.
    readonly property int focusGap: dark ? 2 : 0
    readonly property int focusWidth: 2
    readonly property int focusOuter: dark ? 0 : 1        // extra 1 px ink line outside the ring (Kinari)
    readonly property color focusOuterColor: kinari.ink

    // ---- Legacy names, remapped onto the tokens ---------------------------------------------------------------------
    readonly property color textOnAccent: inkOnAccent

    readonly property color bg: ground
    readonly property color bgSidebar: ground
    readonly property color bgPanel: ground
    // `surface` above doubles as the legacy card / field plane.
    readonly property color surfaceRaised: hover
    readonly property color surfaceHub: surface
    readonly property color tile: placeholder
    readonly property color tileStripe: placeholderRule

    // Game view: always Sumi (design-system.md "Empty stage"), in both themes.
    readonly property color gameBg: sumi.stage
    readonly property color gameHeader: sumi.ground
    readonly property color gameBorder: sumi.rule
    readonly property color gameText: sumi.ink
    readonly property color gameTextMuted: sumi.muted
    readonly property color gameAccent: sumi.accent
    readonly property color gameGroupBg: sumi.ground
    readonly property color gameGroupBorder: sumi.rule
    readonly property color gameHover: sumi.hover
    readonly property color gameTrack: sumi.rule2
    readonly property color gameSegmentOn: sumi.pressed
    readonly property color gameDivider: sumi.rule2
    readonly property color gameSplitDivider: sumi.rule
    readonly property color gamePopover: sumi.surface
    readonly property color gamePopoverBorder: sumi.rule
    readonly property color gameOnBg: sumi.selected
    readonly property color gameOnBorder: sumi.accent
    readonly property color gameDangerBg: sumi.dangerTint
    readonly property color gameDangerBorder: sumi.danger
    readonly property color gameDanger: sumi.danger
    readonly property color gameTile: sumi.stage
    readonly property color gameTileBody: sumi.stage
    readonly property color gameTileEdge: sumi.rule
    readonly property color gameCard: withAlpha(sumi.surface, 0.88)
    readonly property color gameSelection: sumi.ink
    readonly property color gameChip: withAlpha(sumi.surface, 0.88)
    readonly property color gameOk: sumi.ok
    readonly property color gameOkBg: sumi.ground
    readonly property color gameMeta: sumi.dim
    readonly property color gameFaint: sumi.dim
    readonly property color gameKeyLine: sumi.rule
    readonly property color gameBadge: sumi.pressed
    readonly property color gameBadgeText: sumi.ink2
    readonly property color gameOverlayBg: sumi.scrim
    // "Now running" strip in the sidebar
    readonly property color accentBoxBorder: rule
    readonly property color accentOutline: rule

    readonly property color borderSidebar: rule
    readonly property color borderCard: rule
    readonly property color borderInput: rule
    readonly property color borderButton: rule
    readonly property color borderRow: rule2

    readonly property color text: ink
    readonly property color textSecondary: ink2
    readonly property color textMuted: muted
    readonly property color textFaint: dim
    readonly property color textDisabled: disabledInk
    readonly property color monogram: rule

    // One yellow: warn is the accent (as text the Kinari variant accentText), no amber, no cyan. Danger is the muted red.
    readonly property color warn: accentText
    readonly property color warnBg: selected
    readonly property color okBg: ground
    readonly property color error: danger
    readonly property color errorBg: dangerTint
    readonly property color errorText: danger

    readonly property color accentChipBg: selected
    readonly property color accentBorder: rule
    readonly property color infoBg: ground
    readonly property color infoBorder: rule
    readonly property color infoText: ink2
    readonly property color neutralPillBg: pressed
    readonly property color popupBg: surface
    readonly property color borderPopup: rule
    readonly property color textMeta: muted
    readonly property color textTile: dim
    readonly property color toolbarBg: withAlpha(ground, 0.92)

    readonly property color confirmBorder: rule
    readonly property color dangerBorder: danger
    readonly property color rail: rule
    readonly property color rowHover: hover
    readonly property color borderGroup: rule

    // Plugin "warning" tone is the accent (no amber).
    readonly property color pluginWarn: accentText
    readonly property color pluginWarnBg: selected
    readonly property color amberRow: selected
    readonly property color pluginWarnLine: accent
    readonly property color pluginWarnText: ink2
    readonly property color gamePluginWarn: sumi.accent
    readonly property color gamePluginWarnBg: sumi.selected
    readonly property color gamePluginWarnLine: sumi.accent

    // Radii: every one is 0 (the names stay until the screens stop using them).
    readonly property int radius4: 0
    readonly property int radius5: 0
    readonly property int radius6: 0
    readonly property int radius7: 0
    readonly property int radius8: 0
    readonly property int radius9: 0
    readonly property int radius10: 0
    readonly property int radius11: 0
    readonly property int radius12: 0
    readonly property int radius16: 0
    readonly property int radiusPill: 0

    // Legacy gaps. The on-grid ones alias the tokens; 6, 10, 14, 20 and 28 are off the 8 px grid and stay until 3b.
    readonly property int space6: 6
    readonly property int space8: space1
    readonly property int space10: 10
    readonly property int space12: space15
    readonly property int space14: 14
    readonly property int space16: space2
    readonly property int space20: 20
    readonly property int space28: 28

    // Layout
    readonly property int sidebarWidth: sidebar
    readonly property int columnWidth: 300       // second column of 3e / 3f / 3p
    readonly property int detailWidth: 340       // Library detail column
    readonly property int contentMaxWidth: 880   // Settings, Runs and plugin screens: left-aligned, never wider
    readonly property int gridMaxWidth: 1680     // Library grid
    readonly property int inputTestWidth: 340    // Controllers input test
    // SettingsRow grid 14 | flex | 280 | 72; Emulation and Settings share the content width, so the controls end on the same x.
    readonly property int settingsDotSlot: 14
    readonly property int settingsControlWidth: 280
    readonly property int settingsResetWidth: 72
    readonly property int settingsControlHeight: row32
    readonly property int settingsNarrowBelow: 640   // below this row width the control moves under the text

    // Font size roles (legacy names onto the type scale)
    readonly property int fontHero: 40      // start / pairing title (Light 40 on a full screen)
    readonly property int fontDialog: 20    // dialog title
    readonly property int fontPage: 40      // page title
    readonly property int fontDetail: 20    // detail title
    readonly property int fontCard: 16      // card title (header-title)
    readonly property int fontSection: 14   // section / group title (item-title)
    readonly property int fontBody: 14      // body, rows
    readonly property int fontSmall: 13     // controls, secondary rows (input, button-sm)
    readonly property int fontMeta: 12      // meta, help text (small)
    readonly property int fontMono: 11      // eyebrow, slots, badges (meta-11)

    // Motion (short; never in the game view)
    readonly property int durFast: 120
    readonly property int durPage: 160
    readonly property int durList: 180

    // Count with a plural noun, never "(s)": plural(1, "folder", "folders") -> "1 folder", plural(3, ...) -> "3 folders".
    function plural(n: int, one: string, many: string): string {
        return n + " " + (Number(n) === 1 ? one : many)
    }

    function toneColor(tone: string): color {
        switch (tone) {
        case "ok": return ok
        case "warn": return warn
        case "error": return error
        case "info": return ink2
        case "amber": return pluginWarn
        default: return muted
        }
    }
    function toneBg(tone: string): color {
        switch (tone) {
        case "ok": return okBg
        case "info": return accentChipBg
        case "amber": return pluginWarnBg
        case "warn": return warnBg
        case "error": return errorBg
        default: return neutralPillBg
        }
    }

    // ---- Plugin building blocks (docs/design/plugins.md "Building blocks"). Plugins choose tones, size names and player
    // numbers only; the mapping to colors and pixels lives here. `dark` is passed explicitly because in-game surfaces stay
    // dark while the rest of the app follows `Theme.dark`. One yellow: the "warning" tone is the accent, "info" is plain ink.
    function toneFg(tone: string, dark: bool): color {
        switch (tone) {
        case "info": return tok("ink2", dark)
        case "ok": return tok("ok", dark)
        case "warning": return tok("accentText", dark)
        case "danger": return tok("danger", dark)
        default: return tok("muted", dark)
        }
    }
    function toneBgFor(tone: string, dark: bool): color {
        switch (tone) {
        case "info": return tok("ground", dark)
        case "ok": return tok("ground", dark)
        case "warning": return tok("selected", dark)
        case "danger": return tok("dangerTint", dark)
        default: return tok("pressed", dark)
        }
    }
    function toneLine(tone: string, dark: bool): color {
        switch (tone) {
        case "ok": return tok("ok", dark)
        case "warning": return tok("accent", dark)
        case "danger": return tok("danger", dark)
        default: return tok("rule", dark)
        }
    }
    // Gap names of the layout blocks: 4, 8, 12, 16, 20; anything else is rounded down to the nearest allowed value.
    function gapPx(g: var): int {
        const allowed = [4, 8, 12, 16, 20]
        let r = 8
        if (g === undefined || g === null) return r
        r = allowed[0]
        for (let i = 0; i < allowed.length; ++i) {
            if (allowed[i] <= g) r = allowed[i]
        }
        return r
    }
    function pluginImageUrl(pluginId: string, src: string): string {
        if (!src) return ""
        const m = /^(cache|asset):(.+)$/.exec(src)
        return m ? "image://fbplugin/" + pluginId + "/" + m[1] + "/" + m[2] : ""
    }
    // Neutral colors of the blocks by role, for the given theme.
    function blockColor(role: string, dark: bool): color {
        switch (role) {
        case "text": return tok("ink", dark)
        case "textSecondary": return tok("ink2", dark)
        case "textMuted": return tok("muted", dark)
        case "textMeta": return tok("muted", dark)
        case "textFaint": return tok("dim", dark)
        case "textDisabled": return tok("disabledInk", dark)
        case "textOnAccent": return tok("onAccent", dark)
        case "accent": return tok("accent", dark)
        case "surface": return tok("ground", dark)
        case "card": return tok("ground", dark)
        case "cardBorder": return tok("rule", dark)
        case "border": return tok("rule", dark)
        case "borderButton": return tok("rule", dark)
        case "hover": return tok("hover", dark)
        case "selection": return tok("selected", dark)
        case "track": return tok("rule2", dark)
        case "popup": return tok("surface", dark)
        case "popupBorder": return tok("rule", dark)
        case "ownerRing": return tok("ground", dark)
        case "missedLine": return tok("rule", dark)
        case "mapWater": return tok("stage", dark)
        case "mapLand": return tok("placeholder", dark)
        case "scrim": return tok("scrim", dark)
        default: return tok("ink", dark)
        }
    }
}
