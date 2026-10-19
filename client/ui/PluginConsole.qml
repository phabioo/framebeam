import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Plugin console (docs/design/plugins.md 4d-4-2 and 4d-console-error): last tab of the game panel, Developer mode only.
// Plugin select with status dot, Reload, WATCHED, EVENTS, LOG, and the memory regions the core exposes (for checking the
// memory map of a core with a real game). Always dark, like the rest of the game view. Newest events and log lines first.
Item {
    id: root
    required property PlayerController player
    readonly property PluginsController ctl: player.plugins
    readonly property var con: ctl.console
    readonly property var pluginList: con.plugins || []
    readonly property string selected: con.selected || ""
    readonly property string status: con.status || "stopped"
    readonly property bool failed: status === "error" || (status === "paused" && (con.error || "") !== "")
    readonly property var watched: con.watched || []
    readonly property var events: con.events || []
    readonly property var logLines: con.log || []
    readonly property var regions: con.regions || []
    objectName: "pluginConsole"

    function dotColor(st: string): color {
        switch (st) {
        case "running": return Theme.gameOk
        case "error": return Theme.gameDanger
        case "paused": return Theme.gamePluginWarn
        default: return Theme.gameMeta
        }
    }
    function levelColor(level: string): color {
        switch (level) {
        case "warn": return Theme.gamePluginWarn
        case "error": return Theme.gameDanger
        case "debug": return Theme.gameFaint
        default: return Theme.gameText
        }
    }
    function selectedName(): string {
        for (let i = 0; i < pluginList.length; ++i)
            if (pluginList[i].id === selected) return pluginList[i].name
        return selected
    }

    Shortcut {
        sequence: "Ctrl+Shift+R"
        enabled: root.visible
        onActivated: root.ctl.reloadSelected()
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        Column {
            id: col
            x: 20
            y: 16
            width: flick.width - 40
            spacing: 16

            // ---- plugin select + reload
            RowLayout {
                width: parent.width
                spacing: 8
                GameButton {
                    id: selectBtn
                    objectName: "consoleSelect"
                    Layout.fillWidth: true
                    look: "outlined"
                    implicitHeight: 32
                    caret: "▾"
                    text: root.selectedName()
                    onClicked: selectPop.open = !selectPop.open
                    Rectangle {
                        objectName: "consoleStatusDot"
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8; radius: 4
                        color: root.dotColor(root.status)
                    }
                    leftPadding: 24
                }
                GameButton {
                    objectName: "consoleReload"
                    look: "outlined"
                    implicitHeight: 32
                    glyph: "↻"
                    text: qsTr("Reload")
                    tip: qsTr("Reload plugin · Ctrl+Shift+R")
                    onClicked: root.ctl.reloadPlugin(root.selected)
                }
            }

            // ---- error (4d-console-error)
            Rectangle {
                objectName: "consoleError"
                visible: root.failed
                width: parent.width
                implicitHeight: errCol.implicitHeight + 24
                height: implicitHeight
                radius: 8
                color: Theme.gameDangerBg
                border.width: 1
                border.color: Theme.gameDangerBorder
                Column {
                    id: errCol
                    x: 12; y: 12
                    width: parent.width - 24
                    spacing: 8
                    FbLabel {
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Paused after an error · your game keeps running")
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: Theme.gameDanger
                    }
                    FbMono {
                        textFormat: Text.PlainText
                        objectName: "consoleErrorText"
                        width: parent.width
                        wrapMode: Text.WrapAnywhere
                        text: root.con.error || ""
                        font.pixelSize: 11
                        color: Theme.gameText
                        maximumLineCount: 12
                        elide: Text.ElideRight
                    }
                    FbLabel {
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        width: parent.width
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: Theme.gameTextMuted
                        text: qsTr("Panels from this plugin show “Paused”. Fix the file and Reload, or reload as is to try again. Saved with the log in Settings → Diagnostics.")
                    }
                    GameButton {
                        objectName: "consoleRestart"
                        look: "outlined"
                        implicitHeight: 32
                        text: qsTr("Restart plugin")
                        onClicked: root.ctl.reloadPlugin(root.selected)
                    }
                }
            }

            // ---- WATCHED
            Column {
                width: parent.width
                spacing: 6
                RowLayout {
                    width: parent.width
                    Eyebrow { Layout.fillWidth: true; color: Theme.gameFaint; text: qsTr("Watched · %1").arg(root.watched.length) }
                    FbMono { textFormat: Text.PlainText; text: qsTr("live"); font.pixelSize: 11; color: Theme.gameOk }
                }
                FbLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    objectName: "consoleWatchedEmpty"
                    visible: root.watched.length === 0
                    width: parent.width
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: Theme.gameMeta
                    text: qsTr("Nothing watched. Use fb.dev.watch(label, address, kind) in the plugin.")
                }
                Repeater {
                    model: root.watched
                    delegate: Row {
                        required property var modelData
                        objectName: "consoleWatched"
                        spacing: 8
                        FbMono { textFormat: Text.PlainText; width: 92; text: modelData.address; font.pixelSize: 11; color: Theme.gameTextMuted }
                        FbMono { textFormat: Text.PlainText; width: 70; text: modelData.value; font.pixelSize: 11; color: Theme.gameAccent; elide: Text.ElideRight }
                        FbLabel { textFormat: Text.PlainText; width: col.width - 92 - 70 - 16; text: modelData.label; font.pixelSize: 12; color: Theme.gameText; elide: Text.ElideRight }
                    }
                }
            }

            // ---- EVENTS
            Column {
                width: parent.width
                spacing: 6
                Eyebrow { color: Theme.gameFaint; text: qsTr("Events") }
                FbLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    visible: root.events.length === 0
                    width: parent.width
                    font.pixelSize: 12
                    color: Theme.gameMeta
                    text: qsTr("No events yet.")
                }
                Repeater {
                    model: root.events.slice(0, 25)
                    delegate: Row {
                        required property var modelData
                        objectName: "consoleEvent"
                        spacing: 8
                        FbMono { textFormat: Text.PlainText; width: 76; text: modelData.time; font.pixelSize: 11; color: Theme.gameFaint }
                        Rectangle {
                            width: Math.min(110, kindText.implicitWidth + 12); height: 18; radius: 9
                            color: Theme.gameOnBg
                            FbMono { textFormat: Text.PlainText; id: kindText; anchors.centerIn: parent; width: Math.min(implicitWidth, 98); elide: Text.ElideRight; text: modelData.kind; font.pixelSize: 11; color: Theme.gameAccent }
                        }
                        FbMono { textFormat: Text.PlainText; width: col.width - 76 - 110 - 16; text: modelData.text; font.pixelSize: 11; color: Theme.gameText; elide: Text.ElideRight }
                    }
                }
            }

            // ---- LOG
            Column {
                width: parent.width
                spacing: 6
                RowLayout {
                    width: parent.width
                    Eyebrow { Layout.fillWidth: true; color: Theme.gameFaint; text: qsTr("Log") }
                    FbButton {
                        textFormat: Text.PlainText
                        objectName: "consoleCopy"
                        kind: "link"
                        implicitHeight: 24
                        font.pixelSize: 12
                        text: qsTr("Copy")
                        onClicked: root.ctl.copyLog(root.selected)
                    }
                    FbButton {
                        textFormat: Text.PlainText
                        objectName: "consoleClear"
                        kind: "link"
                        implicitHeight: 24
                        font.pixelSize: 12
                        text: qsTr("Clear")
                        onClicked: root.ctl.clearLog(root.selected)
                    }
                }
                FbLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    visible: root.logLines.length === 0
                    width: parent.width
                    font.pixelSize: 12
                    color: Theme.gameMeta
                    text: qsTr("The log is empty.")
                }
                Repeater {
                    model: root.logLines.slice(0, 60)
                    delegate: Row {
                        required property var modelData
                        objectName: "consoleLog_" + modelData.level
                        spacing: 8
                        FbMono { textFormat: Text.PlainText; width: 76; text: modelData.time; font.pixelSize: 11; color: Theme.gameFaint }
                        FbMono { textFormat: Text.PlainText; width: col.width - 76 - 8; text: modelData.text; font.pixelSize: 11; color: root.levelColor(modelData.level); wrapMode: Text.WrapAnywhere; maximumLineCount: 4; elide: Text.ElideRight }
                    }
                }
            }

            // ---- MEMORY MAP
            Column {
                width: parent.width
                spacing: 6
                Eyebrow { color: Theme.gameFaint; text: qsTr("Memory map") }
                FbLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    objectName: "consoleNoRegions"
                    visible: root.regions.length === 0
                    width: parent.width
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: Theme.gameMeta
                    text: root.con.regionsText || qsTr("The core exposes no memory map")
                }
                Repeater {
                    model: root.regions
                    delegate: Row {
                        required property var modelData
                        objectName: "consoleRegion"
                        spacing: 8
                        FbMono { textFormat: Text.PlainText; width: 92; text: modelData.start; font.pixelSize: 11; color: Theme.gameTextMuted }
                        FbMono { textFormat: Text.PlainText; width: 92; text: "+" + modelData.length; font.pixelSize: 11; color: Theme.gameText }
                        FbMono { textFormat: Text.PlainText; width: col.width - 92 - 92 - 16; text: modelData.addrspace !== "" ? modelData.addrspace : "-"; font.pixelSize: 11; color: Theme.gameFaint; elide: Text.ElideRight }
                    }
                }
            }
        }
    }

    AnchoredPopover {
        id: selectPop
        objectName: "consoleSelectPopover"
        anchorItem: selectBtn
        popWidth: Math.min(300, root.width - 24)
        padding: 4
        align: "center"
        onCloseRequested: selectPop.open = false
        Column {
            width: parent.width
            Repeater {
                model: root.pluginList
                delegate: PopoverItem {
                    required property var modelData
                    objectName: "consolePlugin_" + modelData.id
                    width: parent.width
                    text: modelData.name
                    trailing: modelData.statusText
                    current: modelData.id === root.selected
                    onActivated: { root.ctl.selectConsolePlugin(modelData.id); selectPop.open = false }
                }
            }
        }
    }
}
