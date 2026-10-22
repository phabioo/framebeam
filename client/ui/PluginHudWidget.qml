import QtQuick
import Uzume.App

// One HUD widget (docs/design/plugins.md 4e): always dark, surface rgba(17,18,20,.82), border rgba(255,255,255,.08),
// radius 8, padding 6 10, at most 240 wide. It takes no input: hover (or focus) only brightens it and shows the "⋯"
// menu button and, for non-official plugins, the "Unverified" / "Local" marker.
Item {
    id: root
    required property var entry                    // PluginsController.hudWidgets entry
    property string marker: ""
    readonly property bool lit: hover.containsMouse || menuButton.hovered
    signal menuRequested(Item anchor)

    readonly property real maxInner: 240 - 20
    readonly property bool rightSide: entry && entry.corner === "bottom-right"
    readonly property bool topSide: entry && entry.corner === "top-left"
    width: bg.width
    height: bg.height
    objectName: "hudWidget_" + (entry ? entry.pluginId : "")

    Rectangle {
        id: bg
        width: Math.min(240, row.width + 20)
        height: row.height + 12
        radius: 8
        color: root.lit ? Qt.rgba(17 / 255, 18 / 255, 20 / 255, 0.96) : Qt.rgba(17 / 255, 18 / 255, 20 / 255, 0.82)
        border.width: 1
        border.color: root.lit ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(1, 1, 1, 0.08)
        Behavior on color { ColorAnimation { duration: 80 } }

        Row {
            id: row
            x: 10
            y: 6
            spacing: 6
            PluginTabIcon {
                anchors.verticalCenter: parent.verticalCenter
                size: 16
                chipRadius: 4
                name: root.entry ? (root.entry.name || "") : ""
                icon: root.entry ? (root.entry.icon || "") : ""
            }
            PluginBlock {
                id: block
                objectName: "hudBlock"
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, root.maxInner - 22)
                height: implicitHeight
                node: root.entry ? root.entry.tree : ({})
                dark: true
                pluginId: root.entry ? root.entry.pluginId : ""
            }
        }
        // No input to the blocks: this area takes every press; it only tracks the hover.
        MouseArea {
            id: hover
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
            onPressed: (m) => m.accepted = true
        }
        Rectangle {
            id: menuButton
            objectName: "hudMenuButton"
            readonly property bool hovered: btnHover.hovered
            visible: root.lit
            // Beside the widget (P45), never over its text: left of it in the right-hand corner, else right of it
            x: root.rightSide ? -width - 4 : parent.width + 4
            anchors.verticalCenter: parent.verticalCenter
            width: 22; height: 22; radius: 6
            color: btnHover.hovered ? Theme.gameSegmentOn : Qt.rgba(17 / 255, 18 / 255, 20 / 255, 0.9)
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Widget menu")
            Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: "⋯"; font.pixelSize: 15; color: Theme.gameText }
            HoverHandler { id: btnHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.menuRequested(menuButton) }
        }
    }
    Rectangle {
        objectName: "hudMarker"
        visible: root.lit && root.marker !== ""
        // "Unverified" / "Local": above the widget, below it in the top corner so it is never cut off by the play area edge
        x: bg.x
        y: root.topSide ? bg.height + 3 : -height - 3
        width: markerText.implicitWidth + 12
        height: 16
        radius: 4
        color: Theme.gamePluginWarnBg
        border.width: 1
        border.color: Theme.gamePluginWarnLine
        FbLabel { textFormat: Text.PlainText; id: markerText; anchors.centerIn: parent; text: root.marker; font.pixelSize: 10; font.weight: Font.Medium; color: Theme.gamePluginWarn }
    }
}
