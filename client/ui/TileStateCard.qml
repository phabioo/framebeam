import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// State card centred on a Multiview tile (3r-3): "❚❚ Paused by Lena" / "Frozen picture · resumes here", "Jonas’s game is loading" /
// "Connecting". Surface rgba(18,19,21,.88), border #34353a, radius 10, padding 10 16; title 14/600, sub 12 muted.
// `compact` = the PiP window: title only, smaller.
Rectangle {
    id: root
    property string title: ""
    property string sub: ""
    property bool compact: false
    implicitWidth: col.implicitWidth + (compact ? 20 : 32)
    implicitHeight: col.implicitHeight + (compact ? 12 : 20)
    radius: 10
    color: Theme.gameCard
    border.width: 1
    border.color: Theme.gamePopoverBorder
    ColumnLayout {
        id: col
        anchors.centerIn: parent
        width: Math.min(implicitWidth, root.width - 16)
        spacing: 4
        FbLabel {
            objectName: "tileCardTitle"
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: Math.max(60, root.parent ? root.parent.width - 40 : 300)
            elide: Text.ElideRight
            text: root.title
            font.pixelSize: root.compact ? 12 : 14
            font.weight: Font.DemiBold
            color: Theme.gameText
        }
        FbLabel {
            objectName: "tileCardSub"
            visible: root.sub !== "" && !root.compact
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: Math.max(60, root.parent ? root.parent.width - 40 : 300)
            elide: Text.ElideRight
            text: root.sub
            font.pixelSize: 12
            color: Theme.gameMeta
        }
    }
}
