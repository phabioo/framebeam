import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Row of the game table (3c, board S3c0 "TableRow"): 44 px, hairline `rule-2` below, columns title (with the local / paused
// tags) | system | status | added (only with a date sort). Hover = `hover` plane; selected = 2 px Yamabuki marker at the left,
// `selected` tint and title weight 500. The column widths are shared with the table header in LibraryScreen (tableSystemW,
// tableStatusW, tableAddedW); the title column takes the rest and elides.
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
    required property bool onHub          // false: local-only game (file stays on a Player), shows the "Local" tag
    required property bool localFile      // starts from a local file of a library folder; with onHub: "Local · on server"
    required property string addedText   // "DD.MM.YYYY" from the Hub; shown as "Added ..." with a date sort
    property bool showAdded: false
    property bool selected: false
    property bool running: false   // game paused in the background (3c-5: tag in the title cell, status line)

    signal clicked()

    implicitHeight: Theme.row44

    Rectangle {   // hover / selected plane
        anchors.fill: parent
        color: tile.selected ? Theme.selected : (hover.hovered ? Theme.hover : "transparent")
    }
    Rectangle {   // hairline
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.rule2
    }
    Rectangle {   // selection marker (marker-start)
        visible: tile.selected
        width: 2
        height: parent.height
        color: Theme.accent
    }
    // Download progress as a 2 px strip on the hairline.
    Rectangle {
        visible: tile.stateKind === "downloading"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        height: 2
        width: parent.width * Math.max(0, Math.min(1, tile.progress))
        color: Theme.accent
        Behavior on width { NumberAnimation { duration: Theme.durFast } }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space15
        anchors.rightMargin: Theme.space15
        spacing: Theme.space2

        Item {   // title cell: the title, then the tags; a long title shrinks and elides, the tags keep their room
            id: titleCell
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            Layout.preferredHeight: parent.height
            readonly property real tagsWidth: tags.width + (tags.width > 0 ? Theme.space1 : 0)
            UzLabel {
                id: titleText
                objectName: "tileTitle"
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, Math.min(implicitWidth, titleCell.width - titleCell.tagsWidth))
                text: tile.title
                font.weight: tile.selected ? Font.Medium : Font.Normal
                elide: Text.ElideRight
                HoverHandler { id: titleHover }
                ToolTip.visible: titleHover.hovered && truncated
                ToolTip.text: text
                ToolTip.delay: 400
            }
            Row {
                id: tags
                anchors.left: titleText.right
                anchors.leftMargin: tags.width > 0 ? Theme.space1 : 0
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space1
                UzTag {
                    objectName: "tileRunningPill"
                    visible: tile.running
                    small: true
                    tone: "warn"
                    text: qsTr("Paused")
                }
                UzTag {
                    objectName: "tileLocalPill"
                    visible: !tile.onHub || tile.localFile
                    small: true
                    tone: "neutral"
                    text: tile.onHub ? qsTr("Local · on server") : qsTr("Local")
                }
            }
        }
        Item {   // system cell
            Layout.preferredWidth: 56
            Layout.preferredHeight: parent.height
            UzSystemChip {
                anchors.verticalCenter: parent.verticalCenter
                system: tile.system
                visible: tile.system !== ""
            }
        }
        UzLabel {
            objectName: "tileStatus"
            Layout.preferredWidth: 192
            text: tile.running ? qsTr("❚❚ Running · paused") : tile.tileText
            font.pixelSize: Theme.roles.small.size
            font.weight: Font.Medium
            color: tile.running ? Theme.accentText : tile.tileTone === "neutral" ? Theme.muted : Theme.toneColor(tile.tileTone)
            elide: Text.ElideRight
            HoverHandler { id: statusHover }
            ToolTip.visible: statusHover.hovered && truncated
            ToolTip.text: text
            ToolTip.delay: 400
        }
        UzLabel {
            objectName: "tileAdded"
            visible: tile.showAdded && tile.addedText !== ""
            Layout.preferredWidth: 104
            text: qsTr("Added %1").arg(tile.addedText)
            font.family: Theme.mono
            font.pixelSize: Theme.roles.meta11.size
            color: Theme.dim
            elide: Text.ElideRight
        }
        Item {   // keeps the added column's room when a row has no date, so the columns line up
            visible: tile.showAdded && tile.addedText === ""
            Layout.preferredWidth: 104
        }
    }

    // Keyboard (R-P4): Tab reaches the row, Enter/Space selects it.
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
    UzFocusRing {
        objectName: "tileFocusRing"
        visible: tile.activeFocus
        inset: true
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler {
        onTapped: tile.clicked()
    }
}
