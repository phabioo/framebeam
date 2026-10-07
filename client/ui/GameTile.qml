import QtQuick
import QtQuick.Layouts

// Tile in the game grid (3c): square cover with system label and monogram, title, status line with symbol.
Item {
    id: tile
    required property string gameId
    required property string title
    required property string system
    required property string monogram
    required property string stateKind
    required property string tileText
    required property string tileTone
    required property real progress
    property bool selected: false

    signal clicked()

    implicitHeight: cover.height + 62

    Rectangle {
        id: cover
        width: parent.width - 18
        height: width
        radius: Theme.radius8
        color: Theme.tile
        clip: true
        scale: hover.hovered ? 1.015 : 1
        Behavior on scale { NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }

        Repeater {
            model: 6
            delegate: Rectangle {
                required property int index
                width: cover.width * 2
                height: 10
                rotation: -30
                x: -cover.width / 2
                y: index * (cover.height / 3.2) - 10
                color: Theme.tileStripe
                opacity: 0.55
            }
        }
        FbMono {
            x: 12
            y: 10
            text: tile.system
            font.pixelSize: Theme.fontMono
            color: Theme.textTile
        }
        FbLabel {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 12
            text: tile.monogram
            font.pixelSize: 36
            font.weight: Font.DemiBold
            font.letterSpacing: -0.7
            color: Theme.monogram
        }
        Rectangle {
            visible: tile.stateKind === "downloading"
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            height: 4
            width: parent.width * Math.max(0, Math.min(1, tile.progress))
            color: Theme.accent
            Behavior on width { NumberAnimation { duration: Theme.durFast } }
        }
    }
    // Selection: accent border 2 px, offset -3, radius 8 (outside the cover).
    Rectangle {
        anchors.fill: cover
        anchors.margins: -3
        radius: Theme.radius10
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        opacity: tile.selected ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
    }

    ColumnLayout {
        anchors.top: cover.bottom
        anchors.topMargin: 10
        anchors.left: cover.left
        anchors.right: cover.right
        spacing: 3
        FbLabel {
            Layout.fillWidth: true
            text: tile.title
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
        FbLabel {
            objectName: "tileStatus"
            Layout.fillWidth: true
            text: tile.tileText
            font.pixelSize: Theme.fontMeta
            font.weight: Font.Medium
            color: tile.tileTone === "neutral" ? Theme.textMuted : Theme.toneColor(tile.tileTone)
            elide: Text.ElideRight
        }
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler {
        onTapped: tile.clicked()
    }
}
