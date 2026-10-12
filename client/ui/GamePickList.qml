import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// The games a participant may bring into a run (RunsController.games). One is selected (radio).
Rectangle {
    id: root
    required property var rows           // RunsController.games
    property string selectedId: ""
    implicitHeight: Math.min(176, Math.max(52, listCol.implicitHeight + 8))
    radius: Theme.radius8
    color: Theme.surface
    border.width: 1
    border.color: Theme.borderInput
    clip: true

    FbLabel {
        anchors.centerIn: parent
        visible: root.rows.count === 0
        width: parent.width - 24
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: qsTr("No game in your library fits this run.")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
    }
    Flickable {
        anchors.fill: parent
        anchors.margins: 4
        contentWidth: width
        contentHeight: listCol.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: listCol
            width: parent.width
            spacing: 2
            Repeater {
                model: root.rows
                delegate: RunCheckRow {
                    id: gameRow
                    required property var model
                    objectName: "runGame_" + gameRow.model.id
                    radio: true
                    title: gameRow.model.title
                    sub: gameRow.model.system.toUpperCase()
                    checked: root.selectedId === gameRow.model.id
                    onToggled: root.selectedId = gameRow.model.id
                }
            }
        }
    }
}
