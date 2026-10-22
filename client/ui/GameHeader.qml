import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// GameHeader (docs/design/player.md "GameHeader", 3g-2, 3r-2): five fixed zones, the same in Session and Multiview:
// Back | Context | Game controls (your game only) | View | Display. 1280 and below: compact (icons with tooltips; Reset stays in
// the header, only Hotkeys and Pause move into "⋯"). Watching only: no game controls, "Leave" instead. The popovers themselves (Reset, speed, layout,
// Running sessions, "⋯") live in the game screen; this header only reports clicks and shows the open one as pressed.
Rectangle {
    id: root
    objectName: "gameHeader"
    required property PlayerController player
    property bool compact: false
    // `compact` from the window width, or forced when the zones still do not fit with wide fonts (icons, Reset into "⋯").
    property bool forceCompact: false
    readonly property bool tight: compact || forceCompact
    property string popover: ""
    property var hotkeys: ({})
    readonly property GameSession session: player.gameSession
    readonly property SessionController ctl: player.sessions
    readonly property DiagnosticsModel diag: ctl.diagnostics
    readonly property bool own: session.active
    // Multiview: tab "multiview" with a game of your own or several surfaces; one remote Session alone is "watching".
    readonly property bool multi: ctl.tab !== "session" && (own || ctl.surfaceCount > 1)
    readonly property bool narrow: width < 1100
    readonly property bool segmentFocused: viewSegment.activeFocus || modeSegment.activeFocus || modeCycle.activeFocus
    readonly property var layouts: session.active ? session.screenLayouts : (ctl.watching ? ["stacked", "side", "top"] : [])
    readonly property Item resetAnchor: resetButton
    readonly property Item speedAnchor: speedSplit
    readonly property Item layoutAnchor: layoutButton
    readonly property Item addAnchor: addButton
    readonly property Item moreAnchor: moreButton
    signal popoverToggled(string name)
    signal fullscreenRequested()

    color: Theme.gameHeader
    implicitHeight: 56
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.gameBorder }

    function key(label) { return label !== undefined && label !== "" ? " · " + label : "" }
    readonly property string shareText: ctl.shared ? (ctl.visibility === "invite_only" ? qsTr("Invite only · %1 watching") : qsTr("Shared · %1 watching")).arg(ctl.viewerCount)
                                                  : qsTr("Not shared")
    readonly property var watchLink: ctl.surfaceLinks[ctl.surfaceOrder.length > 0 ? ctl.surfaceOrder[0] : ""] || null

    // Wide fonts: when the five zones do not fit at their preferred widths, the optional Multiview texts give way
    // ("Multiview" title first, then the "YOUR GAME" label) instead of the zones overlapping (the context zone is
    // squeezed below its mode switch otherwise and the controls cover it). `need` is the preferred width with both texts
    // shown, derived from the current implicit width, so the result does not depend on what is hidden right now.
    property bool hideMultiTitle: false
    property bool hideYourGame: false
    // Last resort in a tight header that still does not fit (very wide fonts): the key badges next to the display icons go.
    property bool hideHints: false
    readonly property real hintsFullWidth: diagnosticsButton.hintFullWidth + fullscreenButton.hintFullWidth
    property bool hideAddMeta: false   // and, if that is still not enough, the "· 3/4" next to "+ Add"

    // Narrow Multiview header, priority when space runs out (stage 0..4; every width used is measured independent of the
    // current stage, so the result does not flap): 1 the PiP/Row/Grid segment becomes one cycle button (never an empty pill with
    // "…"), 2 the key badges and the "· 3/4" give way, 3 "Multiview" becomes "Multi", 4 the sharing pill shrinks to dot + short word.
    // The game title keeps at least ten characters through all of this (its layout minimum) until the last stage.
    property int fitStage: 0
    readonly property bool modeCollapsed: fitStage >= 1
    readonly property bool stageHints: fitStage >= 2
    readonly property bool shortView: fitStage >= 3
    readonly property bool shortPill: fitStage >= 4
    readonly property bool cycleHidden: fitStage >= 5   // absurdly wide fonts: not even the cycle button fits next to a title and the sharing state
    readonly property string shareShort: ctl.shared ? (ctl.visibility === "invite_only" ? qsTr("Invite only") : qsTr("Shared")) : qsTr("Not shared")
    TextMetrics { id: tmTitle10; text: "abcdefghij"; font.pixelSize: 15; font.weight: Font.DemiBold }
    TextMetrics { id: tmPillFull; text: root.shareText; font.pixelSize: 12; font.weight: Font.Medium }
    TextMetrics { id: tmPillShort; text: root.shareShort; font.pixelSize: 12; font.weight: Font.Medium }
    TextMetrics { id: tmViewLong; text: qsTr("Multiview"); font.pixelSize: 13; font.weight: Font.Medium }
    TextMetrics { id: tmViewShort; text: qsTr("Multi"); font.pixelSize: 13; font.weight: Font.Medium }
    readonly property bool modeAvailable: multi && ctl.availableLayouts.length > 0
    readonly property var modeChoices: ctl.availableLayouts.filter(m => m !== "grid" || ctl.gridAvailable)   // the layouts that can be picked
    // Width the stage savings give back: what is hidden right now is added to the layout's implicit width before judging.
    readonly property real stageSaving: (modeCollapsed ? modeSegment.implicitWidth - (cycleHidden ? 0 : modeCycle.implicitWidth) : 0)
        + (stageHints ? hintsFullWidth + addButton.metaFullWidth : 0) + (shortView ? tmViewLong.advanceWidth - tmViewShort.advanceWidth : 0)
    function computeStage() {
        if (!(multi && own && multiGameTitle.visible)) return 0
        const fixedFull = headerRow.implicitWidth - headerContext.implicitWidth   // everything outside the context zone, as laid out now
            + (hideHints || stageHints ? hintsFullWidth : 0) + (hideAddMeta || stageHints ? addButton.metaFullWidth : 0)
            + (shortView ? tmViewLong.advanceWidth - tmViewShort.advanceWidth : 0)   // ... with every optional text back
        const hintsGain = hintsFullWidth + addButton.metaFullWidth
        const viewGain = tmViewLong.advanceWidth - tmViewShort.advanceWidth
        const pillFull = tmPillFull.advanceWidth + 32, pillShort = tmPillShort.advanceWidth + 32
        const title = tmTitle10.advanceWidth
        const modeFull = modeSegment.implicitWidth + 10, cycle = modeCycle.implicitWidth + 10
        const avail = headerRow.width
        const need = (fixed, pill, mode) => fixed + title + 10 + pill + mode
        if (need(fixedFull, pillFull, modeFull) <= avail) return 0
        if (need(fixedFull, pillFull, cycle) <= avail) return 1
        if (need(fixedFull - hintsGain, pillFull, cycle) <= avail) return 2
        if (need(fixedFull - hintsGain - viewGain, pillFull, cycle) <= avail) return 3
        if (need(fixedFull - hintsGain - viewGain, pillShort, cycle) - title + 40 <= avail) return 4   // title down to about three characters
        return 5
    }
    property real wideNeed: 0   // preferred width without the optional texts, regular (not tight) widths
    // Applied after the layout pass that reported the new size: changing the visibility of the optional texts inside that pass
    // makes the header layout polish twice ("polish loop").
    function updateFit() { Qt.callLater(root.applyFit) }
    function applyFit() {
        const titleW = multiTitle.implicitWidth + 10
        const labelW = yourGame.implicitWidth + 20
        // The game title and the share pill that stand in for them (narrow Multiview headers) are not part of the preferred width: they elide / sit in the space the context zone has.
        const extras = (multiGameTitle.visible ? multiGameTitle.Layout.preferredWidth + headerContext.spacing : 0) + (multiSharePill.visible ? multiSharePill.Layout.minimumWidth + headerContext.spacing : 0)
        const need = headerRow.implicitWidth - extras + (multiTitle.visible ? 0 : titleW) + (yourGame.visible ? 0 : labelW) + root.stageSaving
        if (!root.forceCompact) {
            root.wideNeed = need - titleW - labelW - headerContext.implicitWidth + (root.modeAvailable ? modeSegment.implicitWidth : 0) + root.stageSaving   // the context zone can shrink to its mode switch; measured only at regular widths, so the decision cannot flap
        }
        // Tight: the layout minimum (every zone at its smallest) plus the badges that are hidden right now.
        const minFull = headerRow.Layout.minimumWidth + (root.hideHints || root.stageHints ? root.hintsFullWidth : 0) + (root.hideAddMeta || root.stageHints ? addButton.metaFullWidth : 0)
        const stageOwned = root.multi && root.own && multiGameTitle.visible && root.fitStage < 4   // the stage logic decides first
        root.hideHints = root.tight && !stageOwned && minFull > headerRow.width
        root.hideAddMeta = root.tight && !stageOwned && minFull - root.hintsFullWidth > headerRow.width
        const st = root.computeStage()
        if (st !== root.fitStage) root.fitStage = st
        if (root.compact) {   // compact from the window width: the layout was designed for it, only the optional texts can still give way
            hideMultiTitle = need > headerRow.width
            hideYourGame = need - titleW > headerRow.width
            return
        }
        root.forceCompact = root.wideNeed > headerRow.width
        if (root.forceCompact) {
            hideMultiTitle = true
            hideYourGame = true
        } else {
            hideMultiTitle = need > headerRow.width
            hideYourGame = need - titleW > headerRow.width
        }
    }

    RowLayout {
        id: headerRow
        anchors.fill: parent
        onWidthChanged: root.updateFit()
        onImplicitWidthChanged: root.updateFit()
        Layout.onMinimumWidthChanged: root.updateFit()
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 12

        // 1 Back
        GameButton {
            objectName: "backToLibraryButton"
            muted: true
            look: "flat"
            implicitHeight: 32
            hPad: 8
            fixedWidth: root.tight ? 32 : 0
            text: root.tight ? "" : qsTr("← Library")
            glyph: root.tight ? "←" : ""
            tip: root.own ? (root.tight ? qsTr("Library · game keeps running, paused") : qsTr("Game keeps running, paused"))
                          : qsTr("Leave the Session and go to the Library")
            Accessible.name: qsTr("Library")
            onClicked: root.player.leaveGameView()
        }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 20; color: Theme.gameDivider }

        // 2 Context
        RowLayout {
            id: headerContext
            objectName: "headerContext"
            Layout.fillWidth: true
            // Never narrower than the Multiview mode switch (regular, non-compact windows): when even the collapsed header does not fit (very wide
            // fallback fonts), the right-hand zones run past the window edge instead of covering this switch.
            // The share pill of a narrow Multiview header stays whole next to the (elided) game title.
            Layout.minimumWidth: Math.max(modeSegment.visible ? Math.min(modeSegment.implicitWidth, root.compact ? 90 : 360) : 0,
                                          multiSharePill.visible ? 100 : 0,
                                          // multiview narrow form: the title may elide down to ten characters, the pill and the mode control stay whole
                                          multiGameTitle.visible && root.fitStage < 4 ? multiGameTitle.Layout.minimumWidth + (multiSharePill.visible ? multiSharePill.Layout.minimumWidth + headerContext.spacing : 0)
                                                                   + (modeCycle.visible ? modeCycle.implicitWidth + headerContext.spacing : 0) + headerContext.spacing : 0)
            spacing: 10
            // Session: title + share pill
            UzLabel {
                objectName: "gameTitle"
                visible: root.own && !root.multi
                Layout.minimumWidth: 0
                Layout.maximumWidth: root.tight ? 150 : 240
                text: root.session.title
                elide: Text.ElideRight
                color: Theme.gameText
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            SharePill {
                objectName: "sharePill"
                visible: root.own && !root.multi
                on: root.ctl.shared
                text: root.shareText
            }
            // Watching only
            UzLabel {
                visible: !root.own && !root.multi
                Layout.minimumWidth: 0
                text: qsTr("Watching")
                color: Theme.gameMeta
                font.pixelSize: 13
            }
            UzLabel {
                objectName: "gameTitle"
                visible: !root.own && !root.multi
                Layout.minimumWidth: 0
                text: qsTr("%1 · %2").arg(root.ctl.watchedWho).arg(root.ctl.watchedGame)
                elide: Text.ElideRight
                color: Theme.gameText
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            SharePill {
                objectName: "watchLinkPill"
                visible: !root.own && !root.multi && root.watchLink !== null
                on: root.watchLink ? root.watchLink.tone === "ok" : false
                text: root.watchLink ? root.watchLink.text : ""
            }
            // Multiview: title + mode. When the "Multiview" title and the "YOUR GAME" label give way (narrow windows, wide fonts) the
            // header keeps the game name (elided) and the sharing pill (R-P5); watching only: the "Multiview" title.
            UzLabel {
                id: multiGameTitle
                objectName: "multiGameTitle"
                visible: root.multi && !multiTitle.visible
                Layout.fillWidth: true
                Layout.minimumWidth: root.fitStage < 4 ? Math.min(implicitWidth, tmTitle10.advanceWidth) : 0   // at least ten characters
                Layout.preferredWidth: Math.min(implicitWidth, 120)
                Layout.maximumWidth: Math.min(implicitWidth, root.tight ? 160 : 240)
                elide: Text.ElideRight
                text: root.own ? root.session.title : qsTr("Multiview")
                color: Theme.gameText
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            SharePill {
                id: multiSharePill
                objectName: "multiSharePill"
                visible: root.multi && root.own && !yourGame.visible
                // fillWidth + maximumWidth = preferred: never grows, shrinks (its text elides) before the header overflows
                Layout.fillWidth: true
                Layout.preferredWidth: implicitWidth
                Layout.maximumWidth: implicitWidth
                Layout.minimumWidth: root.fitStage < 4 ? implicitWidth : 0   // only the title gives way (to ten characters), the pill shortens by text first
                on: root.ctl.shared
                text: root.shortPill ? root.shareShort : root.shareText
            }
            UzLabel {
                id: multiTitle
                objectName: "multiviewTitle"
                visible: root.multi && !root.narrow && !root.hideMultiTitle
                Layout.minimumWidth: 0
                elide: Text.ElideRight
                text: qsTr("Multiview")
                color: Theme.gameText
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            GameSegment {
                id: modeSegment
                objectName: "modeSegment"
                // fillWidth + maximumWidth = preferred: never grows, but shrinks with the context zone instead of running past it
                Layout.fillWidth: true
                Layout.preferredWidth: implicitWidth
                Layout.maximumWidth: implicitWidth
                visible: root.modeAvailable && !root.modeCollapsed
                padX: root.tight ? 8 : 11
                current: root.ctl.multiviewMode
                // PiP | Row | Grid 2×2; Grid is disabled below four Sessions ("Grid 2×2 · from 4", ADR 0026 D2)
                options: root.ctl.availableLayouts.map(function (m) {
                    return m === "pip" ? { value: "pip", label: qsTr("PiP"), name: "modePip" }
                         : m === "row" ? { value: "row", label: qsTr("Row"), name: "modeRow" }
                         : { value: "grid", label: root.tight ? (root.ctl.gridAvailable ? qsTr("Grid") : qsTr("Grid · from 4"))   // compact: shorter labels before the segment shrinks to "P…"
                                  : (root.ctl.gridAvailable ? qsTr("Grid 2×2") : qsTr("Grid 2×2 · from 4")), name: "modeGrid", disabled: !root.ctl.gridAvailable }
                })
                onPicked: (v) => root.ctl.multiviewMode = v
            }
            // The mode switch when it would not fit: one button with the current mode, a click picks the next one.
            GameButton {
                id: modeCycle
                objectName: "modeCycle"
                visible: root.modeAvailable && root.modeCollapsed && !root.cycleHidden
                look: "surface"
                hPad: root.shortPill ? 6 : 10   // last stage (very wide fonts): no room for padding and caret either
                readonly property var modes: root.ctl.availableLayouts.filter(m => m !== "grid" || root.ctl.gridAvailable)
                text: root.ctl.multiviewMode === "pip" ? qsTr("PiP") : root.ctl.multiviewMode === "row" ? qsTr("Row") : qsTr("Grid")
                widthText: qsTr("Grid")
                caret: root.shortPill ? "" : "▾"
                tip: qsTr("Multiview layout: %1 · click for the next").arg(text)
                focusPolicy: Qt.TabFocus
                onClicked: {
                    const i = modes.indexOf(root.ctl.multiviewMode)
                    if (modes.length > 0) root.ctl.multiviewMode = modes[(i + 1) % modes.length]
                }
                Rectangle {
                    objectName: "modeCycleFocusRing"
                    visible: modeCycle.activeFocus
                    anchors.fill: parent
                    anchors.margins: -2
                    radius: 0
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.gameAccent
                }
            }
            Item { Layout.fillWidth: true }
        }

        // 3 Game controls (your game) or Leave (watching only)
        Rectangle {
            id: controls
            objectName: "gameControls"
            visible: root.own
            Layout.alignment: Qt.AlignVCenter
            implicitHeight: 34
            implicitWidth: controlsRow.implicitWidth + 4
            radius: 0
            color: Theme.gameGroupBg
            border.width: 1
            border.color: Theme.gameGroupBorder
            RowLayout {
                id: controlsRow
                anchors.fill: parent
                anchors.margins: 2
                spacing: 2
                ColumnLayout {
                    id: yourGame
                    objectName: "yourGameLabel"
                    visible: root.multi && !root.narrow && !root.hideYourGame
                    Layout.leftMargin: 8
                    Layout.rightMargin: 10
                    spacing: 1
                    UzMono { text: qsTr("YOUR GAME"); font.pixelSize: 10; font.weight: Font.Medium; font.letterSpacing: 0.8; color: Theme.gameFaint }
                    SharePill { bare: true; on: root.ctl.shared; text: root.shareText }
                }
                GameButton {
                    objectName: "pauseButton"
                    iconKind: root.session.paused ? "play" : "pause"
                    text: root.session.paused ? qsTr("Resume") : qsTr("Pause")
                    widthText: qsTr("Resume")
                    iconOnly: root.tight
                    hPad: root.tight ? 9 : 10
                    tip: (root.session.paused ? qsTr("Resume") : qsTr("Pause")) + (root.multi ? qsTr(" your game") : "") + " · Esc"
                    enabled: root.session.state === GameSession.Running || root.session.state === GameSession.Paused
                    onClicked: root.session.togglePause()
                }
                Rectangle {
                    id: speedSplit
                    objectName: "speedSplit"
                    visible: root.session.fastForwardAvailable
                    Layout.alignment: Qt.AlignVCenter
                    implicitHeight: 30
                    implicitWidth: splitRow.implicitWidth
                    radius: 0
                    color: root.session.fastForward || root.popover === "speed" ? Theme.gameOnBg : "transparent"
                    readonly property bool on: root.session.fastForward || root.popover === "speed"
                    RowLayout {
                        id: splitRow
                        anchors.fill: parent
                        spacing: 0
                        GameButton {
                            objectName: "fastForwardButton"
                            plain: true
                            iconKind: "speed"
                            text: qsTr("Speed-up")
                            iconOnly: root.tight
                            hPad: root.tight ? 9 : 10
                            on: speedSplit.on
                            tip: qsTr("Speed-up") + root.key(root.hotkeys.speedup)
                            onClicked: root.session.toggleFastForward()
                        }
                        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 16; color: Theme.gameSplitDivider }
                        GameButton {
                            objectName: "speedValueButton"
                            plain: true
                            monoLabel: true
                            hPad: 8
                            text: Math.round(root.session.fastForwardRatio * 10) / 10 + "×"
                            widthText: "8×"
                            caret: "▾"
                            on: speedSplit.on
                            tip: qsTr("Speed-up speed")
                            onClicked: root.popoverToggled("speed")
                        }
                    }
                }
                GameButton {
                    id: resetButton
                    objectName: "resetButton"
                    glyph: "↺"
                    text: qsTr("Reset")
                    iconOnly: root.tight    // 1280: the icon stays in the header (P44, 3r-3), never behind "⋯"
                    hPad: root.tight ? 9 : 10
                    danger: root.popover === "reset"
                    tip: qsTr("Reset game")
                    enabled: root.session.active
                    onClicked: root.popoverToggled("reset")
                }
            }
        }
        GameButton {
            objectName: "leaveButton"
            visible: !root.own
            look: "outlined"
            hPad: 12
            text: qsTr("Leave")
            tip: qsTr("Leave the Session")
            onClicked: root.player.leaveGameView()
        }

        // 4 View
        Rectangle { visible: root.own || addButton.visible; Layout.preferredWidth: 1; Layout.preferredHeight: 20; color: Theme.gameDivider }
        GameSegment {
            id: viewSegment
            objectName: "viewSegment"
            visible: root.own
            // fillWidth + maximumWidth = preferred: never grows, but may shrink (a non-fill item is never shrunk by the layout)
            Layout.fillWidth: true
            Layout.preferredWidth: implicitWidth
            Layout.maximumWidth: implicitWidth
            Layout.minimumWidth: Math.min(implicitWidth, 120)
            padX: 12
            current: root.multi ? "multiview" : "session"
            options: [
                { value: "session", label: qsTr("Session"), name: "tabSession" },
                { value: "multiview", label: root.shortView ? qsTr("Multi") : qsTr("Multiview"), name: "tabMultiview" }
            ]
            onPicked: (v) => root.ctl.tab = v
        }
        GameButton {
            id: addButton
            objectName: "addSessionToggle"
            visible: root.ctl.tab !== "session" && root.ctl.surfaceCount >= 1
            look: "surface"
            hPad: 11
            text: qsTr("+ Add")
            meta: "· " + root.ctl.surfaceCount + "/" + root.ctl.maxSurfaces
            showMeta: !root.hideAddMeta && !root.stageHints
            on: root.popover === "picker"
            tip: qsTr("Running sessions")
            onClicked: root.popoverToggled("picker")
        }

        // 5 Display
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 20; color: Theme.gameDivider }
        Rectangle {
            id: display
            objectName: "displayControls"
            Layout.alignment: Qt.AlignVCenter
            implicitHeight: 34
            implicitWidth: displayRow.implicitWidth + 4
            radius: 0
            color: Theme.gameGroupBg
            border.width: 1
            border.color: Theme.gameGroupBorder
            RowLayout {
                id: displayRow
                anchors.fill: parent
                anchors.margins: 2
                spacing: 2
                GameButton {
                    id: layoutButton
                    objectName: "layoutSwitch"
                    visible: root.layouts.length > 1
                    iconKind: "layout"
                    text: root.multi && !root.tight ? qsTr("All tiles") : ""
                    iconOnly: !(root.multi && !root.tight)
                    caret: "▾"
                    hPad: 9
                    on: root.popover === "layout"
                    tip: root.multi ? qsTr("Screen layout · all tiles") : qsTr("Screen layout · this game only")
                    onClicked: root.popoverToggled("layout")
                }
                GameButton {
                    id: diagnosticsButton
                    showHint: !root.hideHints && !root.stageHints
                    objectName: "diagnosticsButton"
                    iconKind: "diag"
                    text: qsTr("Diagnostics")
                    iconOnly: root.tight || root.multi
                    hint: root.hotkeys.diagnostics || ""
                    hPad: root.tight ? 9 : 10
                    on: root.diag.open
                    tip: qsTr("Diagnostics") + root.key(root.hotkeys.diagnostics)
                    onClicked: root.diag.toggle()
                }
                GameButton {
                    id: fullscreenButton
                    showHint: !root.hideHints && !root.stageHints
                    objectName: "fullscreenButton"
                    iconKind: "fullscreen"
                    text: qsTr("Fullscreen")
                    iconOnly: root.tight || root.multi
                    hint: root.hotkeys.fullscreen || ""
                    hPad: root.tight ? 9 : 10
                    tip: qsTr("Fullscreen") + root.key(root.hotkeys.fullscreen)
                    onClicked: root.fullscreenRequested()
                }
            }
        }
        GameButton {
            id: moreButton
            objectName: "moreButton"
            visible: (root.tight && root.own) || (root.modeAvailable && root.cycleHidden)
            look: "surface"
            fixedWidth: 32
            glyph: "⋯"
            on: root.popover === "more"
            tip: qsTr("More")
            onClicked: root.popoverToggled("more")
        }
    }
}
