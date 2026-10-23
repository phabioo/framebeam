import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// Join a run: pick the game you play in it (the library games that fit the plugins of the run). A UzDialog lg
// (design-system.md "Dialog"): label, title, the plugins of the run, the game list, actions.
UzDialog {
    id: root
    objectName: "runJoinDialog"
    required property RunsController runs
    property string runId: ""
    property string runName: ""
    property string inviteLine: ""
    readonly property bool noGame: root.runs.games.count === 0

    size: "lg"
    label: qsTr("Runs · Invite")
    title: qsTr("Join “%1”?").arg(root.runName)
    text: root.inviteLine !== "" ? root.inviteLine : qsTr("Which game do you play in this run?")

    function openFor(id: string, name: string, line: string) {
        root.runId = id
        root.runName = name
        root.inviteLine = line || ""
        root.runs.prepareGames(id)
        picker.selectedId = root.runs.games.count === 1 ? root.runs.games.get(0).id : ""
        root.open()
    }

    // The library or the server's plugin list may arrive after the dialog opened: a single fitting game is picked then, too,
    // but never over a choice the user made.
    Connections {
        target: root.runs.games
        function onModelReset() {
            if (root.shown && picker.selectedId === "" && root.runs.games.count === 1)
                picker.selectedId = root.runs.games.get(0).id
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        spacing: Theme.space1
        visible: jpRep.count > 0
        UzSectionLabel { Layout.fillWidth: true; text: qsTr("Plugins of this run") }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.rule }
        Repeater {
            id: jpRep
            model: root.runs.joinPlugins
            delegate: Item {
                id: jp
                required property var model
                objectName: "runJoinPlugin_" + jp.model.id
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                implicitHeight: jpCol.implicitHeight + Theme.space15
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule2 }
                ColumnLayout {
                    id: jpCol
                    x: Theme.space15
                    width: parent.width - Theme.space15
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space1
                        UzLabel {
                            Layout.fillWidth: true
                            text: jp.model.name + " " + jp.model.version
                            elide: Text.ElideRight
                            font.weight: Font.Medium
                        }
                        PluginOriginPill {
                            objectName: "runJoinPluginOrigin"
                            origin: jp.model.origin
                            text: jp.model.originText
                        }
                    }
                    UzLabel {
                        Layout.fillWidth: true
                        text: qsTr("It will be able to: %1").arg(jp.model.permissionsText)
                        wrapMode: Text.WordWrap
                        color: Theme.muted
                        font.pixelSize: Theme.roles.small.size
                    }
                }
            }
        }
    }
    UnverifiedNote {
        objectName: "runJoinUnverified"
        visible: root.runs.joinUnverified
        text: root.runs.unverifiedWarning
        Layout.fillWidth: true
    }
    ColumnLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        spacing: Theme.space1
        UzSectionLabel { Layout.fillWidth: true; text: qsTr("Your game") }
        GamePickList {
            id: picker
            objectName: "runJoinGames"
            Layout.fillWidth: true
            rows: root.runs.games
        }
    }
    UzStatusIcon {
        objectName: "runJoinWhy"
        visible: root.noGame
        kind: "warning"
        small: true
        text: qsTr("Join is off until you have the game. Ask a server admin to upload it, or add a library folder that contains it.")
        maxWidth: root.frame.width - 2 * 28 - 22
    }

    actions: [
        UzButton {
            objectName: "runJoinCancel"
            text: qsTr("Cancel")
            onClicked: root.close()
        },
        UzButton {
            objectName: "runJoinConfirm"
            kind: "primary"
            text: qsTr("Join")
            enabled: picker.selectedId !== ""
            onClicked: {
                root.runs.joinRun(root.runId, picker.selectedId)
                root.close()
            }
        }
    ]
}
