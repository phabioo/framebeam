import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// In-game plugin screen (docs/design/plugins.md, "In-game screen sheet"): opened by fb.ui.open("screen"). Covers the body
// below the GameHeader (play area and side panel; in fullscreen the play area). Always dark. The game keeps running
// underneath. Top bar: "← Back to game", plugin icon, screen title and plugin name, key hint Esc; below it the plugin's
// "screen" tree (max 1100 wide, centered, scrolling vertically). Esc and Back emit closeRequested().
Rectangle {
    id: root
    objectName: "pluginScreenSheet"
    required property PlayerController player
    property string pluginId: ""
    readonly property bool open: pluginId !== ""
    readonly property var entry: {
        var e = root.player.plugins.sidebarEntries
        for (var i = 0; i < e.length; ++i) if (e[i].pluginId === root.pluginId) return e[i]
        return ({})
    }
    readonly property bool paused: {
        var t = root.player.plugins.panelTabs
        for (var i = 0; i < t.length; ++i) if (t[i].pluginId === root.pluginId) return !!t[i].paused
        return false
    }
    property var tree: ({})
    signal closeRequested()

    color: Theme.gameBg
    visible: open
    focus: open
    clip: true

    function refresh() { root.tree = root.pluginId !== "" ? root.player.plugins.slotTree(root.pluginId, "screen") : ({}) }
    onPluginIdChanged: { refresh(); if (pluginId !== "") forceActiveFocus() }
    Component.onCompleted: refresh()
    Connections {
        target: root.player.plugins
        function onSlotChanged(id, slot) { if (id === root.pluginId && slot === "screen") root.refresh() }
        function onSidebarEntriesChanged() {
            root.refresh()
            // the plugin stopped or was removed: nothing left to show
            if (root.open && root.entry.pluginId === undefined) root.closeRequested()
        }
    }

    Keys.onEscapePressed: (e) => { e.accepted = true; if (!e.isAutoRepeat) root.closeRequested() }

    // The sheet is opaque to the play area below
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 52
            color: Theme.gameHeader
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.gameBorder }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 16
                spacing: 12
                GameButton {
                    objectName: "pluginScreenBack"
                    muted: true
                    look: "flat"
                    text: qsTr("← Back to game")
                    onClicked: root.closeRequested()
                }
                PluginTabIcon {
                    size: 24
                    chipRadius: 6
                    name: root.entry.name || ""
                    icon: root.entry.icon || ""
                }
                UzLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    objectName: "pluginScreenSheetTitle"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                    text: (root.entry.title || root.entry.name || "") + (root.entry.name && root.entry.title && root.entry.title !== root.entry.name ? "  ·  " + root.entry.name : "")
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    color: Theme.gameText
                }
                UzMono {
                    textFormat: Text.PlainText
                    text: "Esc"
                    font.pixelSize: 11
                    color: Theme.gameMeta
                }
            }
        }

        Rectangle {
            objectName: "pluginScreenSheetPaused"
            Layout.fillWidth: true
            visible: root.paused
            implicitHeight: bannerRow.implicitHeight + 20
            color: Theme.gamePluginWarnBg
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.gamePluginWarnLine }
            RowLayout {
                id: bannerRow
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 16
                spacing: 10
                UzLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    Layout.fillWidth: true
                    text: qsTr("Paused · showing its last state")
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    color: Theme.gamePluginWarn
                }
                GameButton {
                    look: "outlined"
                    text: qsTr("Restart plugin")
                    onClicked: root.player.plugins.reloadPlugin(root.pluginId)
                }
            }
        }

        Flickable {
            id: flick
            objectName: "pluginScreenSheetFlick"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: block.implicitHeight + 48
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            PluginBlock {
                id: block
                objectName: "pluginScreenSheetBlock"
                readonly property real avail: flick.width - 48
                width: Math.min(1100, Math.max(0, avail))
                x: Math.max(24, (flick.width - width) / 2)
                y: 24
                height: implicitHeight
                node: root.tree
                dark: true
                pluginId: root.pluginId
                opacity: root.paused ? 0.45 : 1
                enabled: !root.paused
                onAction: (a, v) => root.player.plugins.sendAction(root.pluginId, "screen", a, v)
            }
            UzLabel {
                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                x: 24
                y: 24
                width: flick.width - 48
                visible: !root.paused && root.tree.type === undefined
                text: qsTr("Nothing to show yet.")
                font.pixelSize: 13
                color: Theme.gameTextMuted
            }
        }
    }
}
