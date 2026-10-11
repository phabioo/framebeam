import QtQuick
import FrameBeam.Player

// Plugin icon chip (docs/design/plugins.md 4e): the plugin's icon.png, else its monogram in mono, on a surface chip.
// Used by the panel tabs, the rail, HUD widgets, the tile strip and the sidebar. `dot` adds the 6 px tab dot
// ("news" = cyan, "paused" = amber) at the top right.
Item {
    id: root
    property string name: ""
    property string icon: ""
    property string dot: ""
    property bool dark: true
    property int size: 18
    property int chipRadius: 5

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    Accessible.ignored: true

    Rectangle {
        anchors.fill: parent
        radius: root.chipRadius
        color: root.dark ? "#232428" : Theme.surfaceRaised
        clip: true
        Image {
            id: img
            anchors.fill: parent
            source: root.icon
            visible: status === Image.Ready
            fillMode: Image.PreserveAspectFit
            smooth: false
            sourceSize.width: root.size * 2
            sourceSize.height: root.size * 2
        }
        FbMono {
            visible: !img.visible
            anchors.centerIn: parent
            text: root.name.length > 0 ? root.name.charAt(0).toUpperCase() : "?"
            font.pixelSize: Math.round(root.size * 0.58)
            font.weight: Font.Medium
            color: root.dark ? Theme.gameBadgeText : Theme.textSecondary
        }
    }
    Rectangle {
        objectName: "tabDot"
        visible: root.dot !== ""
        x: parent.width - width + 2
        y: -2
        width: 8; height: 8; radius: 4
        color: root.dot === "paused" ? Theme.gamePluginWarn : Theme.gameAccent
        border.width: 2
        border.color: Theme.gameHeader
    }
}
