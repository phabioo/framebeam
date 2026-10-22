import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// Content of a plugin tab in the game panel (docs/design/plugins.md 4e): the plugin's "panel" tree rendered dark. A paused
// plugin keeps its last tree, dimmed and without input, under a "Paused" banner with "Restart plugin".
Item {
    id: root
    required property PlayerController player
    required property var tab                      // entry of PluginsController.panelTabs
    readonly property string pluginId: tab.pluginId || ""
    readonly property bool paused: !!tab.paused
    property var tree: ({})

    function refresh() { root.tree = root.player.plugins.slotTree(root.pluginId, "panel") }
    Component.onCompleted: refresh()
    onPluginIdChanged: refresh()
    Connections {
        target: root.player.plugins
        function onSlotChanged(id, slot) { if (id === root.pluginId && slot === "panel") root.refresh() }
        function onPanelTabsChanged() { root.refresh() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            objectName: "pluginPausedBanner"
            Layout.fillWidth: true
            visible: root.paused
            implicitHeight: bannerRow.implicitHeight + 20
            color: Theme.gamePluginWarnBg
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.gamePluginWarnLine }
            RowLayout {
                id: bannerRow
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
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
                    objectName: "pluginRestartButton"
                    look: "outlined"
                    text: qsTr("Restart plugin")
                    onClicked: root.player.plugins.reloadPlugin(root.pluginId)
                }
            }
        }

        Flickable {
            id: flick
            objectName: "pluginPanelFlick"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: block.implicitHeight + 40
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            PluginBlock {
                id: block
                objectName: "pluginPanelBlock"
                x: 20
                y: 20
                width: flick.width - 40
                height: implicitHeight
                node: root.tree
                dark: true
                pluginId: root.pluginId
                opacity: root.paused ? 0.45 : 1
                enabled: !root.paused
                onAction: (a, v) => root.player.plugins.sendAction(root.pluginId, "panel", a, v)
            }
            UzLabel {
                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                x: 20
                y: 20
                width: flick.width - 40
                visible: !root.paused && root.tree.type === undefined
                text: qsTr("Nothing to show yet.")
                wrapMode: Text.WordWrap
                font.pixelSize: 13
                color: Theme.gameTextMuted
            }
        }
    }
}
