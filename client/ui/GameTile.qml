import QtQuick
import QtQuick.Layouts

// Kachel im Spielraster (3c): quadratisches Monogramm, Titel, Status.
Item {
    id: tile
    required property string gameId
    required property string title
    required property string system
    required property string monogram
    required property string stateKind
    required property string statusText
    required property string statusTone
    required property real progress
    property bool selected: false

    signal clicked()

    implicitHeight: cover.height + 62

    Rectangle {
        id: cover
        width: parent.width - 18
        height: width
        radius: 8
        color: Theme.tile
        clip: true

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
            font.pixelSize: 11
            color: Theme.textFaint
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
        }
    }
    Rectangle {
        visible: tile.selected
        anchors.fill: cover
        anchors.margins: -3
        radius: 10
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
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
        RowLayout {
            spacing: 6
            StatusDot { tone: tile.statusTone }
            FbLabel {
                Layout.fillWidth: true
                text: tile.statusText
                font.pixelSize: 12
                color: tile.statusTone === "neutral" ? Theme.textMuted : Theme.toneColor(tile.statusTone)
                font.weight: tile.statusTone === "error" ? Font.Medium : Font.Normal
                elide: Text.ElideRight
            }
        }
    }

    TapHandler {
        onTapped: tile.clicked()
    }
}
