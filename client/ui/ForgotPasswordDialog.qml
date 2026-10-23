import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// 8g-1 Forgot password. There is no email reset: a member asks an admin, an admin runs `uzume-server reset-admin` on the device
// that runs the server (Copy; the Docker form below). The texts come from AccountsController::forgotPassword.
UzDialog {
    id: root
    required property PlayerController player
    readonly property var info: player.accounts.forgotPassword
    property string copied: ""
    label: qsTr("Sign in")
    title: qsTr("Forgot your password?")
    text: info.text || ""

    function copyText(which: string, value: string) {
        clip.text = value
        clip.selectAll()
        clip.copy()
        root.copied = which
        copiedReset.restart()
    }
    TextEdit { id: clip; visible: false }
    Timer { id: copiedReset; interval: 1600; onTriggered: root.copied = "" }

    ColumnLayout {
        objectName: "forgotCommands"
        visible: root.info.isAdmin === true
        Layout.fillWidth: true
        spacing: Theme.space1
        Repeater {
            model: [{ key: "command", title: qsTr("On the device that runs the server"), value: root.info.command || "" },
                    { key: "docker", title: qsTr("With Docker"), value: root.info.dockerCommand || "" }]
            delegate: ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: Theme.space05
                Text {
                    text: modelData.title
                    textFormat: Text.PlainText
                    font.family: Theme.sans
                    font.pixelSize: Theme.roles.fieldLabel.size
                    font.weight: Font.Medium
                    color: Theme.ink
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space1
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Theme.row36
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.rule
                        Text {
                            objectName: "forgotCommand_" + modelData.key
                            anchors.fill: parent
                            anchors.leftMargin: Theme.space15
                            anchors.rightMargin: Theme.space15
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            text: modelData.value
                            textFormat: Text.PlainText
                            font.family: Theme.mono
                            font.pixelSize: Theme.roles.address.size
                            color: Theme.ink
                        }
                    }
                    UzButton {
                        objectName: "forgotCopy_" + modelData.key
                        text: root.copied === modelData.key ? qsTr("Copied") : qsTr("Copy")
                        onClicked: root.copyText(modelData.key, modelData.value)
                    }
                }
            }
        }
    }
    actions: [
        UzButton { objectName: "forgotClose"; kind: "primary"; text: qsTr("Close"); onClicked: root.close() }
    ]
}
