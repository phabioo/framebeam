import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Confirmation before a local save file becomes the Hub's current save. Modal; "Cancel" is the default and focused
// action, Esc = cancel. The current checkpoint stays in the history (the Hub secures it as "Before upload").
Item {
    id: root
    required property PlayerController player
    readonly property var request: player.saveHistory.uploadRequest
    readonly property bool shown: request.path !== undefined

    visible: shown
    anchors.fill: parent
    z: 100

    onShownChanged: if (shown) cancelButton.forceActiveFocus()

    Rectangle {
        anchors.fill: parent
        color: Theme.dark ? "#cc08090a" : "#99000000"
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: (wheel) => wheel.accepted = true
    }

    FocusScope {
        anchors.fill: parent
        focus: root.shown
        Keys.onEscapePressed: root.player.saveHistory.cancelUploadFile()

        Rectangle {
            objectName: "uploadDialog"
            anchors.centerIn: parent
            width: Math.min(520, parent.width - 48)
            implicitHeight: col.implicitHeight + 56
            height: Math.min(implicitHeight, parent.height - 32)
            radius: 12
            color: Theme.bgPanel
            border.width: 1
            border.color: Theme.borderInput

            ColumnLayout {
                id: col
                anchors.fill: parent
                anchors.margins: 28
                spacing: 16

                Eyebrow { text: qsTr("Upload save file") }
                FbLabel {
                    objectName: "uploadDialogTitle"
                    Layout.fillWidth: true
                    text: qsTr("Upload \"%1\" as the current save?").arg(root.request.fileName || "")
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }
                FbLabel {
                    objectName: "uploadDialogDetail"
                    Layout.fillWidth: true
                    text: qsTr("%1 · %2 · slot \"%3\"").arg(root.request.sizeText || "").arg(root.request.gameTitle || "").arg(root.request.slot || "")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                FbLabel {
                    Layout.fillWidth: true
                    text: qsTr("The current Hub version moves to the save history. Your local save is backed up before it is replaced.")
                    color: Theme.textMuted
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Item { Layout.fillWidth: true }
                    FbButton {
                        id: cancelButton
                        objectName: "uploadCancel"
                        text: qsTr("Cancel")
                        KeyNavigation.tab: confirmButton
                        KeyNavigation.backtab: confirmButton
                        Keys.onReturnPressed: root.player.saveHistory.cancelUploadFile()
                        onClicked: root.player.saveHistory.cancelUploadFile()
                    }
                    FbButton {
                        id: confirmButton
                        objectName: "uploadConfirm"
                        kind: "primary"
                        text: qsTr("Upload")
                        KeyNavigation.tab: cancelButton
                        KeyNavigation.backtab: cancelButton
                        Keys.onReturnPressed: root.player.saveHistory.confirmUploadFile()
                        onClicked: root.player.saveHistory.confirmUploadFile()
                    }
                }
            }
        }
    }
}
