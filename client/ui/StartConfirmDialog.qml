import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Confirmation before a different game starts while one is paused in the background (0.7.x). Modal; "Cancel" is the
// default and focused action, Esc = cancel. Confirm quits the running game (saved and synced first) and starts the other.
Item {
    id: root
    required property PlayerController player
    readonly property var request: player.startConfirm
    readonly property bool shown: request.active === true

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
        Keys.onEscapePressed: root.player.cancelQuitAndStart()

        Rectangle {
            objectName: "startConfirmDialog"
            anchors.centerIn: parent
            width: Math.min(520, parent.width - 48)
            implicitHeight: col.implicitHeight + 56
            height: Math.min(implicitHeight, parent.height - 32)
            radius: Theme.radius12
            color: Theme.bgPanel
            border.width: 1
            border.color: Theme.borderInput

            ColumnLayout {
                id: col
                anchors.fill: parent
                anchors.margins: 28
                spacing: 16

                Eyebrow { text: qsTr("Game running") }
                FbLabel {
                    objectName: "startConfirmTitle"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("Quit %1 and start %2?").arg(root.request.runningTitle || "").arg(root.request.newTitle || "")
                    font.pixelSize: Theme.fontDialog
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }
                FbLabel {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("%1 is saved and synced first.").arg(root.request.runningTitle || "")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
                    FbButton {
                        id: cancelButton
                        objectName: "startConfirmCancel"
                        text: qsTr("Cancel")
                        KeyNavigation.tab: confirmButton
                        KeyNavigation.backtab: confirmButton
                        Keys.onReturnPressed: root.player.cancelQuitAndStart()
                        onClicked: root.player.cancelQuitAndStart()
                    }
                    FbButton {
                        id: confirmButton
                        objectName: "startConfirmQuit"
                        kind: "primary"
                        text: qsTr("Quit and start")
                        KeyNavigation.tab: cancelButton
                        KeyNavigation.backtab: cancelButton
                        Keys.onReturnPressed: root.player.confirmQuitAndStart()
                        onClicked: root.player.confirmQuitAndStart()
                    }
                }
            }
        }
    }
}
