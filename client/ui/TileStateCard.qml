import QtQuick
import QtQuick.Layouts
import Uzume.App

// State card centred on a Multiview tile (3r-3): "❚❚ Paused by Lena" / "Frozen picture · resumes here", "Jonas’s game is loading" /
// "Connecting". `surface` plane (88 %) in a 1 px `rule` frame, square; title item-title, sub small, dim.
// `compact` = the PiP window: title only, smaller.
Rectangle {
    id: root
    property string title: ""
    property string sub: ""
    property bool compact: false
    // The card never grows past its tile: long names wrap onto a second line instead of eliding ("Paused by Le…", R-P7).
    readonly property real maxCardWidth: Math.max(120, root.parent ? root.parent.width - 16 : 400)
    readonly property real pad: compact ? Theme.space3 : Theme.space4
    implicitWidth: Math.min(maxCardWidth, col.implicitWidth + pad)
    implicitHeight: col.implicitHeight + (compact ? Theme.space15 : Theme.space3)
    color: Theme.withAlpha(Theme.surface, 0.88)
    border.width: 1
    border.color: Theme.rule
    ColumnLayout {
        id: col
        anchors.centerIn: parent
        width: root.width - root.pad
        spacing: Theme.space05
        UzLabel {
            objectName: "tileCardTitle"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: root.compact ? 1 : 2
            elide: Text.ElideRight
            text: root.title
            font.pixelSize: root.compact ? Theme.roles.small.size : Theme.roles.itemTitle.size
            font.weight: Theme.roles.itemTitle.weight
            color: Theme.ink
        }
        UzLabel {
            objectName: "tileCardSub"
            visible: root.sub !== "" && !root.compact
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            text: root.sub
            font.pixelSize: Theme.roles.small.size
            color: Theme.dim
        }
    }
}
