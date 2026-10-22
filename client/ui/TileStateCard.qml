import QtQuick
import QtQuick.Layouts
import Uzume.App

// State card centred on a Multiview tile (3r-3): "❚❚ Paused by Lena" / "Frozen picture · resumes here", "Jonas’s game is loading" /
// "Connecting". Surface rgba(18,19,21,.88), border #34353a, radius 10, padding 10 16; title 14/600, sub 12 muted.
// `compact` = the PiP window: title only, smaller.
Rectangle {
    id: root
    property string title: ""
    property string sub: ""
    property bool compact: false
    // The card never grows past its tile: long names wrap onto a second line instead of eliding ("Paused by Le…", R-P7).
    readonly property real maxCardWidth: Math.max(120, root.parent ? root.parent.width - 16 : 400)
    readonly property real pad: compact ? 20 : 32
    implicitWidth: Math.min(maxCardWidth, col.implicitWidth + pad)
    implicitHeight: col.implicitHeight + (compact ? 12 : 20)
    radius: 0
    color: Theme.gameCard
    border.width: 1
    border.color: Theme.gamePopoverBorder
    ColumnLayout {
        id: col
        anchors.centerIn: parent
        width: root.width - root.pad
        spacing: 4
        UzLabel {
            objectName: "tileCardTitle"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: root.compact ? 1 : 2
            elide: Text.ElideRight
            text: root.title
            font.pixelSize: root.compact ? 12 : 14
            font.weight: Font.DemiBold
            color: Theme.gameText
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
            font.pixelSize: 12
            color: Theme.gameMeta
        }
    }
}
