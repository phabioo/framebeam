import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

ApplicationWindow {
    id: window
    required property PlayerController player

    visible: true
    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 600
    title: qsTr("Uzume")
    color: Theme.bg

    // Appearance (Settings): Dark | Light | System; the game view is always dark.
    Binding {
        target: Theme
        property: "dark"
        value: window.player.darkMode || window.player.screen === "game"
    }

    // Update banner (S6): never shown in the game view; the install action is refused while a game runs.
    header: Rectangle {
        objectName: "updateBanner"
        visible: window.player.updates.bannerText !== "" && window.player.screen !== "game"
        implicitHeight: visible ? 44 : 0
        height: implicitHeight
        color: Theme.surface
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderCard }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 12
            StatusDot { tone: "ok" }
            UzLabel {
                objectName: "updateBannerText"
                Layout.fillWidth: true
                text: window.player.updates.bannerText
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }
            UzButton {
                objectName: "updateBannerAction"
                visible: window.player.updates.bannerAction !== "none"
                implicitHeight: 30
                kind: "primary"
                text: window.player.updates.bannerActionLabel
                onClicked: window.player.updates.bannerAction === "link" ? window.player.updates.openNotes()
                                                                         : window.player.updates.install()
            }
            UzButton {
                objectName: "updateBannerDismiss"
                implicitHeight: 30
                kind: "link"
                text: qsTr("Later")
                onClicked: window.player.updates.dismissBanner()
            }
        }
    }

    StackLayout {
        id: stack
        anchors.fill: parent
        currentIndex: ["connection", "pairing", "library", "game", "settings", "emulation", "controllers", "plugin", "runs", "server"].indexOf(window.player.screen)

        ConnectionScreen { player: window.player }
        PairingScreen { player: window.player }
        LibraryScreen { player: window.player }
        GameScreen { player: window.player }
        SettingsScreen { id: settingsScreen; player: window.player }
        EmulationScreen { player: window.player }
        ControllersScreen { player: window.player }
        PluginScreen { player: window.player }
        RunsScreen { player: window.player }
        ServerScreen { player: window.player }
    }

    // Hotkeys (frame.md "0-4 G"): Ctrl+1 to 4 Library, Runs, Emulation, Controllers; Ctrl+, Settings. Not in the game view,
    // not before a server is connected.
    readonly property bool navHotkeys: ["library", "runs", "emulation", "controllers", "settings", "plugin", "server"].indexOf(window.player.screen) >= 0
    Shortcut { sequence: "Ctrl+1"; enabled: window.navHotkeys; onActivated: window.player.showLibrary() }
    Shortcut { sequence: "Ctrl+2"; enabled: window.navHotkeys && window.player.runs.available; onActivated: window.player.showRuns() }
    Shortcut { sequence: "Ctrl+3"; enabled: window.navHotkeys; onActivated: window.player.showEmulation() }
    Shortcut { sequence: "Ctrl+4"; enabled: window.navHotkeys; onActivated: window.player.showControllers() }
    Shortcut { sequence: "Ctrl+,"; enabled: window.navHotkeys; onActivated: window.player.showSettings() }

    // Connection check (6e): modal over every screen; "Edit Hub address…" opens the inline edit of the Hub in Settings.
    ConnectionCheckDialog { player: window.player }
    Connections {
        target: window.player.connectionCheck
        function onEditHubRequested() {
            window.player.showSettings()
            settingsScreen.editActiveHubAddress()
        }
    }

    // fb.ui.open("screen") outside the game view: the plugin's sidebar screen (in the game view GameScreen shows the sheet).
    Connections {
        target: window.player.plugins
        function onOpenRequested(pluginId, slot) {
            if (slot === "screen" && window.player.screen !== "game") window.player.showPluginScreen(pluginId)
        }
    }

    // Page transition: short fade-in of the page that becomes current. Never in the game view.
    NumberAnimation { id: pageFade; property: "opacity"; from: 0; to: 1; duration: Theme.durPage; easing.type: Easing.OutCubic }
    Connections {
        target: window.player
        function onScreenChanged() {
            if (window.player.screen === "game") {
                return
            }
            const page = stack.children[stack.currentIndex]
            if (page !== undefined) {
                pageFade.target = page
                pageFade.restart()
            }
        }
    }

    // Emulation > Uzume > "Fullscreen on start": the window goes fullscreen while a game is shown and returns afterwards.
    property int visibilityBeforeGame: Window.Windowed
    property bool fullscreenApplied: false
    Connections {
        target: window.player
        function onScreenChanged() {
            if (window.player.screen === "game" && window.player.fullscreenOnStart && !window.fullscreenApplied) {
                window.visibilityBeforeGame = window.visibility
                window.fullscreenApplied = true
                window.visibility = Window.FullScreen
            } else if (window.player.screen !== "game" && window.fullscreenApplied) {
                window.fullscreenApplied = false
                window.visibility = window.visibilityBeforeGame === Window.FullScreen ? Window.Windowed : window.visibilityBeforeGame
            }
        }
    }

    ConflictDialog { player: window.player }
    UzConfirmDialog { controller: window.player.admin }   // "Confirm it's you": opens for any admin request that needs it
    ReportProblemDialog { player: window.player }  // Settings > Diagnostics and the game "⋯" menu open it

    // Tooltips (R-P12): the shared tooltip of the attached ToolTip type is restyled once with the theme tokens
    // (popup surface, popup border, text) instead of the default white box.
    Component {
        id: tipBackground
        Rectangle {
            radius: Theme.radius6
            color: Theme.popupBg
            border.width: 1
            border.color: Theme.borderPopup
        }
    }
    Component {
        id: tipContent
        Text {
            property var tip: null
            text: tip ? tip.text : ""
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontMeta
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }
    Item {
        id: tipHost
        visible: false
        Component.onCompleted: {
            const tip = ToolTip.toolTip
            if (tip === null || tip === undefined) return
            tip.background = tipBackground.createObject(tipHost)
            tip.contentItem = tipContent.createObject(tipHost, { tip: tip })
            tip.padding = 8
            tip.topPadding = 5
            tip.bottomPadding = 5
            tip.z = 1000
        }
    }
}
