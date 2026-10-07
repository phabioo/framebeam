import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Confirmation before a save version is restored (ADR 0012 D7). Modal; "Cancel" is the default and focused action,
// Esc = cancel. The current checkpoint stays in the history (the Hub secures it as "Before restore").
Item {
    id: root
    required property PlayerController player
    readonly property var request: player.saveHistory.restoreRequest
    readonly property bool shown: request.version !== undefined

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
        Keys.onEscapePressed: root.player.saveHistory.cancelRestore()

        Rectangle {
            objectName: "restoreDialog"
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

                Eyebrow { text: qsTr("Restore save version") }
                FbLabel {
                    objectName: "restoreDialogTitle"
                    Layout.fillWidth: true
                    text: qsTr("Restore %1 as the current save?").arg(root.request.versionText || "")
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }
                FbLabel {
                    Layout.fillWidth: true
                    text: qsTr("%1 · %2 · %3").arg(root.request.reasonText || "").arg(root.request.device || "").arg(root.request.when || "")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                FbLabel {
                    visible: (root.request.label || "") !== ""
                    Layout.fillWidth: true
                    text: qsTr("Label: %1").arg(root.request.label || "")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
                FbLabel {
                    Layout.fillWidth: true
                    text: qsTr("The current save of slot \"%1\" stays in the history and this device downloads the restored version. Other devices see it as a newer save.").arg(root.request.slot || "")
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
                        objectName: "restoreCancel"
                        text: qsTr("Cancel")
                        KeyNavigation.tab: confirmButton
                        KeyNavigation.backtab: confirmButton
                        Keys.onReturnPressed: root.player.saveHistory.cancelRestore()
                        onClicked: root.player.saveHistory.cancelRestore()
                    }
                    FbButton {
                        id: confirmButton
                        objectName: "restoreConfirm"
                        kind: "primary"
                        text: qsTr("Restore")
                        KeyNavigation.tab: cancelButton
                        KeyNavigation.backtab: cancelButton
                        Keys.onReturnPressed: root.player.saveHistory.confirmRestore()
                        onClicked: root.player.saveHistory.confirmRestore()
                    }
                }
            }
        }
    }
}
