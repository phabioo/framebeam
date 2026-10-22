import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Tile in the game grid (3c): square cover with system label and monogram, title, status line with symbol.
Item {
    id: tile
    objectName: "gameTile_" + gameId
    required property string gameId
    required property string title
    required property string system
    required property string monogram
    required property string stateKind
    required property string tileText
    required property string tileTone
    required property real progress
    required property bool onHub          // false: local-only game (file stays on a Player), shows the "Local" pill
    required property bool localFile      // starts from a local file of a library folder; with onHub: "Local · on Hub"
    required property string addedText   // "DD.MM.YYYY" from the Hub; shown as "Added ..." with a date sort
    property bool showAdded: false
    property bool selected: false
    property bool running: false   // game paused in the background (3c-5: marker on the cover, status line)

    signal clicked()

    implicitHeight: cover.height + 62 + (showAdded ? 16 : 0)

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
        UzMono {
            x: 12
            y: 10
            text: tile.system
            font.pixelSize: Theme.fontMono
            color: Theme.textTile
        }
        UzLabel {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 12
            text: tile.monogram
            font.pixelSize: 36
            font.weight: Font.DemiBold
            font.letterSpacing: -0.7
            color: Theme.monogram
        }
        // Running marker (3c-5): pill "Paused" top right of the cover, two 2 x 8 bars as the pause icon.
        Rectangle {
            objectName: "tileRunningPill"
            visible: tile.running
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 10
            height: 22
            width: Math.min(parent.width - 20, pillRow.implicitWidth + 16)
            radius: Theme.radiusPill
            color: Theme.accentChipBg
            Row {
                id: pillRow
                anchors.centerIn: parent
                spacing: 6
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Rectangle { width: 2; height: 8; color: Theme.accent }
                    Rectangle { width: 2; height: 8; color: Theme.accent }
                }
                Text {
                    text: qsTr("Paused")
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: Theme.fontMono
                    font.weight: Font.DemiBold
                    color: Theme.accentText
                }
            }
        }
        // "Local" pill (0.12): the game's file is not stored on the Hub.
        Rectangle {
            objectName: "tileLocalPill"
            visible: !tile.onHub || tile.localFile
            // Top right (below the running pill when both show), never over the monogram at the bottom left (R-P22).
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 10
            anchors.topMargin: tile.running ? 38 : 10
            height: 22
            width: Math.min(parent.width - 20, localText.implicitWidth + 16)
            radius: Theme.radiusPill
            color: tile.onHub ? Theme.accentChipBg : Theme.neutralPillBg
            Text {
                id: localText
                anchors.centerIn: parent
                text: tile.onHub ? qsTr("Local · on Hub") : qsTr("Local")
                font.pixelSize: Theme.fontMono
                font.weight: Font.DemiBold
                color: tile.onHub ? Theme.accentText : Theme.textMuted
            }
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
        UzLabel {
            Layout.fillWidth: true
            text: tile.title
            font.weight: Font.Medium
            elide: Text.ElideRight
            HoverHandler { id: titleHover }
            ToolTip.visible: titleHover.hovered && truncated
            ToolTip.text: text
            ToolTip.delay: 400
        }
        UzLabel {
            objectName: "tileStatus"
            Layout.fillWidth: true
            text: tile.running ? qsTr("❚❚ Running · paused") : tile.tileText
            font.pixelSize: Theme.fontMeta
            font.weight: Font.Medium
            color: tile.running ? Theme.accentText : tile.tileTone === "neutral" ? Theme.textMuted : Theme.toneColor(tile.tileTone)
            elide: Text.ElideRight
            HoverHandler { id: statusHover }
            ToolTip.visible: statusHover.hovered && truncated
            ToolTip.text: text
            ToolTip.delay: 400
        }
        UzLabel {
            objectName: "tileAdded"
            visible: tile.showAdded && tile.addedText !== ""
            Layout.fillWidth: true
            text: qsTr("Added %1").arg(tile.addedText)
            font.pixelSize: Theme.fontMeta
            color: Theme.textFaint
            elide: Text.ElideRight
        }
    }

    // Keyboard (R-P4): Tab reaches the tile, Enter/Space selects it; the focus ring is drawn around the cover.
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: tile.title + ", " + tile.tileText
    Accessible.onPressAction: tile.clicked()
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            tile.clicked()
            event.accepted = true
        }
    }
    Rectangle {
        objectName: "tileFocusRing"
        visible: tile.activeFocus
        anchors.fill: cover
        anchors.margins: -5
        radius: Theme.radius10 + 2
        color: "transparent"
        border.width: 2
        border.color: Theme.text
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler {
        onTapped: tile.clicked()
    }
}
