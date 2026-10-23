import QtQuick
import Uzume.App

// Plugin icon chip (docs/design/legacy/plugins.md 4e): the plugin's icon.png, else its monogram in mono, on a surface chip.
// Used by the panel tabs, the rail, HUD widgets, the tile strip and the sidebar. `dot` adds the 6 px tab dot
// ("news" = Yamabuki, "paused" = accent text) at the top right.
Item {
    id: root
    property string name: ""
    property string icon: ""
    property string dot: ""
    property bool dark: Theme.dark
    property int size: 18
    property int chipRadius: 0   // ignored: the design system has no rounded corners (callers in GamePanel still pass 0)

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    Accessible.ignored: true

    Rectangle {
        anchors.fill: parent
        color: Theme.tok("pressed", root.dark)
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
        UzMono {
            textFormat: Text.PlainText
            visible: !img.visible
            anchors.centerIn: parent
            text: root.name.length > 0 ? root.name.charAt(0).toUpperCase() : "?"
            font.pixelSize: Math.round(root.size * 0.58)
            font.weight: Font.Medium
            color: Theme.ink2
        }
    }
    Rectangle {
        objectName: "tabDot"
        visible: root.dot !== ""
        x: parent.width - width + 2
        y: -2
        width: 8; height: 8; radius: 0
        color: root.dot === "paused" ? Theme.accentText : Theme.accent
        border.width: 2
        border.color: Theme.ground
    }
}
