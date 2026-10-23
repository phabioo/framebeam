import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// Plugin console (docs/design/plugins.md 4d-4-2 and 4d-console-error): last tab of the game panel, Developer mode only.
// Plugin select with status dot, Reload, WATCHED, EVENTS, LOG, and the memory regions the core exposes (for checking the
// memory map of a core with a real game). In the active theme tokens (the game view is dark). Newest events and log lines first.
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
        case "running": return Theme.ok
        case "error": return Theme.danger
        case "paused": return Theme.accentText
        default: return Theme.dim
        }
    }
    function levelColor(level: string): color {
        switch (level) {
        case "warn": return Theme.accentText
        case "error": return Theme.danger
        case "debug": return Theme.dim
        default: return Theme.ink
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
        contentHeight: col.implicitHeight + 2 * Theme.space3
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        Column {
            id: col
            x: Theme.space3
            y: Theme.space2
            width: flick.width - 2 * Theme.space3
            spacing: Theme.space3

            // ---- plugin select + reload
            RowLayout {
                width: parent.width
                spacing: Theme.space1
                // Select field (design-system.md "Select", 36 px); the list is the anchored popover below.
                Rectangle {
                    id: selectBtn
                    objectName: "consoleSelect"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: Theme.row36
                    color: selectPop.open ? Theme.pressed : (Theme.dark ? "transparent" : Theme.surface)
                    border.width: 1
                    border.color: selectPop.open ? Theme.ink : (selectHover.hovered ? Theme.ink2 : Theme.rule)
                    activeFocusOnTab: true
                    Accessible.role: Accessible.ComboBox
                    Accessible.name: root.selectedName()
                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                            selectPop.open = !selectPop.open; event.accepted = true
                        }
                    }
                    Rectangle {
                        objectName: "consoleStatusDot"
                        x: Theme.space15
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8
                        color: root.dotColor(root.status)
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space15 + 8 + Theme.space1
                        anchors.right: chevron.left
                        anchors.rightMargin: Theme.space1
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        text: root.selectedName()
                        font.family: Theme.sans
                        font.pixelSize: Theme.roles.input.size
                        color: Theme.ink
                    }
                    UzGlyph {
                        id: chevron
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space15
                        anchors.verticalCenter: parent.verticalCenter
                        name: selectPop.open ? "chevronUp" : "chevronDown"
                        size: 14
                        color: Theme.muted
                    }
                    HoverHandler { id: selectHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: selectPop.open = !selectPop.open }
                    UzFocusRing { visible: selectBtn.activeFocus }
                }
                UzButton {
                    objectName: "consoleReload"
                    property string tip: qsTr("Reload plugin · Ctrl+Shift+R")
                    text: qsTr("Reload")
                    onClicked: root.ctl.reloadPlugin(root.selected)
                    ToolTip.visible: hovered
                    ToolTip.text: tip
                    ToolTip.delay: 600
                    Accessible.description: tip
                }
            }

            // ---- error (board 4d-2 "Paused after errors"): an icon and a sentence, never a tinted banner
            Column {
                id: errBox
                objectName: "consoleError"
                visible: root.failed
                width: parent.width
                spacing: Theme.space15
                UzStatusIcon {
                    kind: "error"
                    text: qsTr("Paused after an error · your game keeps running")
                    maxWidth: errBox.width
                }
                UzMono {
                    textFormat: Text.PlainText
                    objectName: "consoleErrorText"
                    width: parent.width
                    wrapMode: Text.WrapAnywhere
                    text: root.con.error || ""
                    font.pixelSize: Theme.roles.meta11.size
                    color: Theme.ink
                    maximumLineCount: 12
                    elide: Text.ElideRight
                }
                UzLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    width: parent.width
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.roles.small.size
                    color: Theme.muted
                    text: qsTr("Panels from this plugin show “Paused”. Fix the file and Reload, or reload as is to try again. Saved with the log in Settings → Diagnostics.")
                }
                UzButton {
                    objectName: "consoleRestart"
                    kind: "primary"
                    size: "sm"
                    text: qsTr("Restart plugin")
                    onClicked: root.ctl.reloadPlugin(root.selected)
                }
            }

            // ---- WATCHED
            Column {
                width: parent.width
                spacing: Theme.space1
                RowLayout {
                    width: parent.width
                    Eyebrow { Layout.fillWidth: true; color: Theme.dim; text: qsTr("Watched · %1").arg(root.watched.length) }
                    UzMono { textFormat: Text.PlainText; text: qsTr("live"); font.pixelSize: Theme.roles.meta11.size; color: Theme.ok }
                }
                UzLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    objectName: "consoleWatchedEmpty"
                    visible: root.watched.length === 0
                    width: parent.width
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.roles.small.size
                    color: Theme.dim
                    text: qsTr("Nothing watched. Use fb.dev.watch(label, address, kind) in the plugin.")
                }
                Repeater {
                    model: root.watched
                    delegate: Row {
                        required property var modelData
                        objectName: "consoleWatched"
                        spacing: Theme.space1
                        UzMono { textFormat: Text.PlainText; width: 92; text: modelData.address; font.pixelSize: Theme.roles.meta11.size; color: Theme.muted }
                        UzMono { textFormat: Text.PlainText; width: 70; text: modelData.value; font.pixelSize: Theme.roles.meta11.size; color: Theme.accentText; elide: Text.ElideRight }
                        UzLabel { textFormat: Text.PlainText; width: col.width - 92 - 70 - 16; text: modelData.label; font.pixelSize: Theme.roles.small.size; color: Theme.ink; elide: Text.ElideRight }
                    }
                }
            }

            // ---- EVENTS
            Column {
                width: parent.width
                spacing: Theme.space1
                Eyebrow { color: Theme.dim; text: qsTr("Events") }
                UzLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    visible: root.events.length === 0
                    width: parent.width
                    font.pixelSize: Theme.roles.small.size
                    color: Theme.dim
                    text: qsTr("No events yet.")
                }
                Repeater {
                    model: root.events.slice(0, 25)
                    delegate: Row {
                        required property var modelData
                        objectName: "consoleEvent"
                        spacing: Theme.space1
                        UzMono { textFormat: Text.PlainText; width: 76; text: modelData.time; font.pixelSize: Theme.roles.meta11.size; color: Theme.dim }
                        UzTag {
                            width: Math.min(110, implicitWidth)
                            small: true
                            tone: "accent"
                            icon: "none"
                            text: modelData.kind
                        }
                        UzMono { textFormat: Text.PlainText; width: col.width - 76 - 110 - 16; text: modelData.text; font.pixelSize: Theme.roles.meta11.size; color: Theme.ink; elide: Text.ElideRight }
                    }
                }
            }

            // ---- LOG
            Column {
                width: parent.width
                spacing: Theme.space1
                RowLayout {
                    width: parent.width
                    Eyebrow { Layout.fillWidth: true; color: Theme.dim; text: qsTr("Log") }
                    UzButton {
                        textFormat: Text.PlainText
                        objectName: "consoleCopy"
                        kind: "link"
                        implicitHeight: 24
                        font.pixelSize: Theme.roles.small.size
                        text: qsTr("Copy")
                        onClicked: root.ctl.copyLog(root.selected)
                    }
                    UzButton {
                        textFormat: Text.PlainText
                        objectName: "consoleClear"
                        kind: "link"
                        implicitHeight: 24
                        font.pixelSize: Theme.roles.small.size
                        text: qsTr("Clear")
                        onClicked: root.ctl.clearLog(root.selected)
                    }
                }
                UzLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    visible: root.logLines.length === 0
                    width: parent.width
                    font.pixelSize: Theme.roles.small.size
                    color: Theme.dim
                    text: qsTr("The log is empty.")
                }
                Repeater {
                    model: root.logLines.slice(0, 60)
                    delegate: Row {
                        required property var modelData
                        objectName: "consoleLog_" + modelData.level
                        spacing: Theme.space1
                        UzMono { textFormat: Text.PlainText; width: 76; text: modelData.time; font.pixelSize: Theme.roles.meta11.size; color: Theme.dim }
                        UzMono { textFormat: Text.PlainText; width: col.width - 76 - 8; text: modelData.text; font.pixelSize: Theme.roles.meta11.size; color: root.levelColor(modelData.level); wrapMode: Text.WrapAnywhere; maximumLineCount: 4; elide: Text.ElideRight }
                    }
                }
            }

            // ---- MEMORY MAP
            Column {
                width: parent.width
                spacing: Theme.space1
                Eyebrow { color: Theme.dim; text: qsTr("Memory map") }
                UzLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    objectName: "consoleNoRegions"
                    visible: root.regions.length === 0
                    width: parent.width
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.roles.small.size
                    color: Theme.dim
                    text: root.con.regionsText || qsTr("The core exposes no memory map")
                }
                Repeater {
                    model: root.regions
                    delegate: Row {
                        required property var modelData
                        objectName: "consoleRegion"
                        spacing: Theme.space1
                        UzMono { textFormat: Text.PlainText; width: 92; text: modelData.start; font.pixelSize: Theme.roles.meta11.size; color: Theme.muted }
                        UzMono { textFormat: Text.PlainText; width: 92; text: "+" + modelData.length; font.pixelSize: Theme.roles.meta11.size; color: Theme.ink }
                        UzMono { textFormat: Text.PlainText; width: col.width - 92 - 92 - 16; text: modelData.addrspace !== "" ? modelData.addrspace : "-"; font.pixelSize: Theme.roles.meta11.size; color: Theme.dim; elide: Text.ElideRight }
                    }
                }
            }
        }
    }

    AnchoredPopover {
        id: selectPop
        objectName: "consoleSelectPopover"
        anchorItem: selectBtn
        popWidth: Math.min(300, root.width - 2 * Theme.space15)
        padding: Theme.space05
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
