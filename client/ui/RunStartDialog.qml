import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// "Start a run" (mode "start": name, plugins the Hub offers with run.state, players to invite, your game) and "Invite
// players" into an open run (mode "invite").
Popup {
    id: root
    objectName: "runStartDialog"
    required property RunsController runs
    property string mode: "start"      // start | invite
    property string inviteRunId: ""
    readonly property bool inviting: mode === "invite"

    parent: Window.window ? Window.window.contentItem : null
    anchors.centerIn: parent
    modal: true
    dim: true
    focus: true
    width: Math.min(520, parent ? parent.width - 32 : 520)
    height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, parent ? parent.height - 48 : 640)
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function openStart() {
        root.mode = "start"
        nameField.text = ""
        game.selectedId = ""
        root.runs.beginStart()
        root.open()
        nameField.forceActiveFocus()
    }
    function openInvite(runId: string) {
        root.mode = "invite"
        root.inviteRunId = runId
        root.runs.beginStart()
        root.open()
    }
    onClosed: root.runs.cancelStart()
    Connections {
        target: root.runs
        function onRunCreated() { root.close() }
        function onStartStateChanged() {
            if (root.inviting && root.visible && !root.runs.startBusy && root.runs.startError === "" && inviteWasSent) {
                root.close()
            }
        }
    }
    property bool inviteWasSent: false

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
            text: root.inviting ? qsTr("Invite players") : qsTr("Start a run")
            font.pixelSize: Theme.fontCard
            font.weight: Font.DemiBold
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Math.min(body.implicitHeight, 420)
            contentWidth: width
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: body
                width: parent.width - 8
                spacing: Theme.space10

                Eyebrow { visible: !root.inviting; text: qsTr("Name") }
                FbField {
                    id: nameField
                    objectName: "runNameField"
                    visible: !root.inviting
                    Layout.fillWidth: true
                    placeholderText: qsTr("e.g. Summer Duo")
                    maximumLength: 64
                    font.family: Qt.application.font.family
                }

                Eyebrow { visible: !root.inviting; text: qsTr("Plugins") }
                FbLabel {
                    visible: !root.inviting && root.runs.startPlugins.count === 0
                    Layout.fillWidth: true
                    text: qsTr("Your Hub offers no plugin for runs yet.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                Repeater {
                    model: root.inviting ? null : root.runs.startPlugins
                    delegate: RunCheckRow {
                        id: plugRow
                        required property var model
                        objectName: "runPlugin_" + plugRow.model.id
                        title: plugRow.model.name + " " + plugRow.model.version
                        sub: plugRow.model.description
                        checked: plugRow.model.checked
                        onToggled: root.runs.toggleStartPlugin(plugRow.model.id)
                    }
                }

                Eyebrow { text: qsTr("Players") }
                FbLabel {
                    visible: root.runs.startUsersLoading
                    text: qsTr("Loading players…")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                }
                FbLabel {
                    visible: !root.runs.startUsersLoading && root.runs.startUsers.count === 0
                    Layout.fillWidth: true
                    text: qsTr("There is nobody else to invite on this Hub.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                Repeater {
                    model: root.runs.startUsers
                    delegate: RunCheckRow {
                        id: userRow
                        required property var model
                        objectName: "runUser_" + userRow.model.id
                        title: userRow.model.name
                        sub: userRow.model.online ? qsTr("Online") : ""
                        checked: userRow.model.checked
                        onToggled: root.runs.toggleStartUser(userRow.model.id)
                    }
                }

                Eyebrow { visible: !root.inviting; text: qsTr("Your game") }
                GamePickList {
                    id: game
                    objectName: "runStartGames"
                    visible: !root.inviting
                    Layout.fillWidth: true
                    rows: root.runs.games
                }
            }
        }

        FbLabel {
            objectName: "runStartError"
            visible: root.runs.startError !== ""
            Layout.fillWidth: true
            text: root.runs.startError
            color: Theme.error
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSmall
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space8
            Item { Layout.fillWidth: true }
            FbButton {
                objectName: "runStartCancel"
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            FbButton {
                objectName: "runStartConfirm"
                kind: "primary"
                text: root.inviting ? qsTr("Invite") : qsTr("Start run")
                busy: root.runs.startBusy
                onClicked: {
                    if (root.inviting) {
                        root.inviteWasSent = true
                        root.runs.inviteSelected(root.inviteRunId)
                    } else {
                        root.runs.startRun(nameField.text, game.selectedId)
                    }
                }
            }
        }
    }
    onOpened: inviteWasSent = false
}
