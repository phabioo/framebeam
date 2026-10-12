import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Join a run: pick the game you play in it (the library games that fit the plugins of the run).
Popup {
    id: root
    objectName: "runJoinDialog"
    required property RunsController runs
    property string runId: ""
    property string runName: ""

    parent: Window.window ? Window.window.contentItem : null
    anchors.centerIn: parent
    modal: true
    dim: true
    focus: true
    width: Math.min(440, parent ? parent.width - 32 : 440)
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function openFor(id: string, name: string) {
        root.runId = id
        root.runName = name
        root.runs.prepareGames(id)
        picker.selectedId = root.runs.games.count === 1 ? root.runs.games.get(0).id : ""
        root.open()
    }

    background: Rectangle {
        radius: Theme.radius12
        color: Theme.popupBg
        border.width: 1
        border.color: Theme.borderPopup
    }

    contentItem: ColumnLayout {
        spacing: Theme.space12
        FbLabel {
            Layout.fillWidth: true
            text: qsTr("Join \"%1\"").arg(root.runName)
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontCard
            font.weight: Font.DemiBold
        }
        FbLabel {
            Layout.fillWidth: true
            text: qsTr("Which game do you play in this run?")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
        GamePickList {
            id: picker
            objectName: "runJoinGames"
            Layout.fillWidth: true
            rows: root.runs.games
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space8
            Item { Layout.fillWidth: true }
            FbButton {
                objectName: "runJoinCancel"
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            FbButton {
                objectName: "runJoinConfirm"
                kind: "primary"
                text: qsTr("Join")
                enabled: picker.selectedId !== ""
                onClicked: {
                    root.runs.joinRun(root.runId, picker.selectedId)
                    root.close()
                }
            }
        }
    }
}
