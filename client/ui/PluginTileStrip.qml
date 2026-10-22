import QtQuick
import QtQuick.Controls.Basic
import Uzume.App

// Tile strip of a remote Multiview tile (docs/design/plugins.md 4e): 20 high, shows the first text row of the plugin's tile
// tree (with the player colour dot of its first avatar and "▾"); a click or A expands it to at most 300 x 120 over the top
// screen with a header strip ("▴"). Always dark. One strip per tile; the tile "⋯" menu (in MultiviewArea) picks the plugin.
Item {
    id: root
    required property PlayerController player
    required property string tileId
    required property var strip                    // {pluginId, name, icon, origin, tree}
    property real maxWidth: 300
    property bool expanded: false
    readonly property string pluginId: strip.pluginId || ""
    readonly property string headline: findText(strip.tree)
    readonly property int dotColor: findColor(strip.tree)
    readonly property string marker: player.plugins.originMarker(strip.origin || "")

    function findText(n) {
        if (!n) return ""
        if (n.type === "text" && n.text) return String(n.text)
        var c = n.children || []
        for (var i = 0; i < c.length; ++i) {
            var r = findText(c[i])
            if (r !== "") return r
        }
        return ""
    }
    function findColor(n) {
        if (!n) return 0
        if (n.type === "avatar" && n.color) return n.color
        var c = n.children || []
        for (var i = 0; i < c.length; ++i) {
            var r = findColor(c[i])
            if (r > 0) return r
        }
        return 0
    }

    objectName: "tileStrip_" + tileId
    width: expanded ? Math.min(300, maxWidth) : Math.min(headerRow.implicitWidth + 16, maxWidth)
    height: expanded ? Math.min(120, 20 + Math.max(0, body.implicitHeight) + 16) : 20
    z: expanded ? 5 : 1

    Rectangle {
        anchors.fill: parent
        radius: 0
        color: Theme.gameChip
        border.width: 1
        border.color: root.expanded ? Theme.gameKeyLine : Qt.rgba(1, 1, 1, 0.08)
        clip: true

        PluginFocusable {
            id: header
            objectName: "tileStripHeader"
            dark: true
            ringRadius: 5
            width: parent.width
            height: 20
            onActivated: root.expanded = !root.expanded
            Row {
                id: headerRow
                x: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                PluginTabIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    size: 14
                    chipRadius: 3
                    name: root.strip.name || ""
                    icon: root.strip.icon || ""
                }
                Rectangle {
                    visible: root.dotColor > 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: 7; height: 7; radius: 0
                    color: Theme.playerColor(root.dotColor, true)
                }
                UzLabel {
                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                    objectName: "tileStripText"
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, Math.max(0, Math.min(300, root.maxWidth) - 56 - (root.dotColor > 0 ? 13 : 0)))
                    elide: Text.ElideRight
                    text: root.headline
                    font.pixelSize: 11
                    color: Theme.gameText
                }
            }
            UzLabel {
                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: root.expanded ? "▴" : "▾"
                font.pixelSize: 10
                color: Theme.gameTextMuted
            }
        }
        Rectangle {
            visible: root.expanded && root.marker !== ""
            anchors.right: parent.right
            anchors.rightMargin: 24
            y: 3
            width: markerLabel.implicitWidth + 10
            height: 14
            radius: 0
            color: Theme.gamePluginWarnBg
            UzLabel { textFormat: Text.PlainText; id: markerLabel; anchors.centerIn: parent; text: root.marker; font.pixelSize: 9; color: Theme.gamePluginWarn }
        }

        Flickable {
            id: flick
            visible: root.expanded
            y: 20
            width: parent.width
            height: parent.height - 20
            clip: true
            contentWidth: width
            contentHeight: body.implicitHeight + 16
            boundsBehavior: Flickable.StopAtBounds
            PluginBlock {
                id: body
                objectName: "tileStripBlock"
                x: 8
                y: 8
                width: flick.width - 16
                height: implicitHeight
                node: root.strip.tree
                dark: true
                pluginId: root.pluginId
                onAction: (a, v) => root.player.plugins.sendAction(root.pluginId, "tile:" + root.tileId, a, v)
            }
        }
    }
}
