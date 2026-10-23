import QtQuick
import QtQuick.Layouts
import Uzume.App

// Confirmation before a different game starts while one is paused in the background (0.7.x). Modal; "Cancel" is the
// default and focused action, Esc = cancel. Confirm quits the running game (saved and synced first) and starts the other.
// A UzDialog (design-system.md "Dialog"): label, title, text, the steps that follow, actions.
UzDialog {
    id: root
    objectName: "startConfirm"
    required property PlayerController player
    readonly property var request: player.startConfirm

    label: qsTr("%1 is running · paused").arg(request.runningTitle || "")
    title: qsTr("Quit %1 and start %2?").arg(request.runningTitle || "").arg(request.newTitle || "")
    text: qsTr("%1 is saved and synced first.").arg(request.runningTitle || "")

    // The dialog is open while the controller has a pending request; Esc and the close button cancel it.
    Binding { target: root; property: "shown"; value: root.request.active === true }
    onClosed: root.player.cancelQuitAndStart()
    // The frame keeps the name the tests and the layout checks look for.
    Component.onCompleted: frame.objectName = "startConfirmDialog"
    onShownChanged: if (shown) Qt.callLater(() => cancelButton.forceActiveFocus())

    // What happens after confirming (3c-5): the steps are shown ahead, the start happens in place.
    ColumnLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        spacing: Theme.space1
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.rule2 }
        Eyebrow { Layout.topMargin: Theme.space05; text: qsTr("After confirming") }
        Repeater {
            model: [
                qsTr("%1 is saved").arg(root.request.runningTitle || ""),
                qsTr("Syncing to the server"),
                qsTr("Starting %1").arg(root.request.newTitle || "")
            ]
            delegate: RowLayout {
                required property string modelData
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: Theme.space1
                Rectangle {
                    Layout.preferredWidth: 8
                    Layout.preferredHeight: 8
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.disabledInk
                }
                UzLabel {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: modelData
                    elide: Text.ElideRight
                    color: Theme.muted
                    font.pixelSize: Theme.roles.input.size
                }
            }
        }
    }

    actions: [
        UzButton {
            id: cancelButton
            objectName: "startConfirmCancel"
            text: qsTr("Cancel")
            KeyNavigation.tab: confirmButton
            KeyNavigation.backtab: confirmButton
            Keys.onReturnPressed: root.player.cancelQuitAndStart()
            onClicked: root.player.cancelQuitAndStart()
        },
        UzButton {
            id: confirmButton
            objectName: "startConfirmQuit"
            // Deterministic width: natural width, at most 360 and at most what is left next to Cancel, so a long title (or a
            // wide font) elides in the button instead of spilling out of the dialog.
            width: Math.max(0, Math.min(implicitWidth, 360, root.frame.width - 2 * 28 - cancelButton.width - Theme.space1))
            kind: "primary"
            text: qsTr("Quit and start %1").arg(root.request.newTitle || "")
            KeyNavigation.tab: cancelButton
            KeyNavigation.backtab: cancelButton
            Keys.onReturnPressed: root.player.confirmQuitAndStart()
            onClicked: root.player.confirmQuitAndStart()
        }
    ]
}
