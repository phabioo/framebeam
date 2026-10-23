import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// The games a participant may bring into a run (RunsController.games): hairline radio rows under a `rule` line, one selected.
// The list scrolls inside a fixed height when there are many games.
Item {
    id: root
    required property var rows           // RunsController.games
    property string selectedId: ""
    implicitHeight: root.rows.count === 0 ? Theme.row44 * 2 : Math.min(Theme.row44 * 4 + 8, listCol.implicitHeight + 1)
    clip: true

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.rule }
    UzLabel {
        anchors.centerIn: parent
        visible: root.rows.count === 0
        width: parent.width - 2 * Theme.space15
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: qsTr("No game in your library fits this run.")
        color: Theme.muted
        font.pixelSize: Theme.roles.input.size
    }
    Flickable {
        anchors.fill: parent
        anchors.topMargin: 1
        contentWidth: width
        contentHeight: listCol.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: listCol
            width: parent.width
            spacing: 0
            Repeater {
                model: root.rows
                delegate: RunCheckRow {
                    id: gameRow
                    required property var model
                    objectName: "runGame_" + gameRow.model.id
                    radio: true
                    title: gameRow.model.title
                    sub: gameRow.model.hint !== "" ? gameRow.model.system.toUpperCase() + " · " + gameRow.model.hint : gameRow.model.system.toUpperCase()
                    checked: root.selectedId === gameRow.model.id
                    onToggled: root.selectedId = gameRow.model.id
                }
            }
        }
    }
}
