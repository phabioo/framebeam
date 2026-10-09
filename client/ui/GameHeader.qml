import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// GameHeader (docs/design/player.md "GameHeader", 3g-2, 3r-2): five fixed zones, the same in Session and Multiview:
// Back | Context | Game controls (your game only) | View | Display. 1280 and below: compact (icons with tooltips, Reset and
// Hotkeys move into "⋯"). Watching only: no game controls, "Leave" instead. The popovers themselves (Reset, speed, layout,
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
    property real wideNeed: 0   // preferred width without the optional texts, regular (not tight) widths
    function updateFit() {
        const titleW = multiTitle.implicitWidth + 10
        const labelW = yourGame.implicitWidth + 20
        const need = headerRow.implicitWidth + (multiTitle.visible ? 0 : titleW) + (yourGame.visible ? 0 : labelW)
        if (!root.forceCompact) {
            root.wideNeed = need - titleW - labelW - headerContext.implicitWidth + (modeSegment.visible ? modeSegment.implicitWidth : 0)   // the context zone can shrink to its mode switch; measured only at regular widths, so the decision cannot flap
        }
        if (root.compact) return   // compact from the window width: the layout was designed for it
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
            Layout.minimumWidth: modeSegment.visible && !root.compact ? modeSegment.implicitWidth : 0
            spacing: 10
            // Session: title + share pill
            FbLabel {
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
            FbLabel {
                visible: !root.own && !root.multi
                Layout.minimumWidth: 0
                text: qsTr("Watching")
                color: Theme.gameMeta
                font.pixelSize: 13
            }
            FbLabel {
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
            // Multiview: title + mode
            FbLabel {
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
                visible: root.multi && root.ctl.availableLayouts.length > 0
                padX: root.tight ? 8 : 11
                current: root.ctl.multiviewMode
                options: root.ctl.availableLayouts.map(function (m) {
                    return m === "pip" ? { value: "pip", label: qsTr("PiP"), name: "modePip" }
                         : m === "side" ? { value: "side", label: qsTr("Side by side"), name: "modeSide" }
                         : { value: "grid", label: qsTr("Grid 2×2"), name: "modeGrid" }
                })
                onPicked: (v) => root.ctl.multiviewMode = v
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
            radius: 9
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
                    FbMono { text: qsTr("YOUR GAME"); font.pixelSize: 10; font.weight: Font.Medium; font.letterSpacing: 0.8; color: Theme.gameFaint }
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
                    radius: 7
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
                    visible: !root.tight
                    glyph: "↺"
                    text: qsTr("Reset")
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
            objectName: "viewSegment"
            visible: root.own
            padX: 12
            current: root.multi ? "multiview" : "session"
            options: [
                { value: "session", label: qsTr("Session"), name: "tabSession" },
                { value: "multiview", label: qsTr("Multiview"), name: "tabMultiview" }
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
            radius: 9
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
            visible: root.tight && root.own
            look: "surface"
            fixedWidth: 32
            glyph: "⋯"
            on: root.popover === "more"
            tip: qsTr("More")
            onClicked: root.popoverToggled("more")
        }
    }
}
