import QtQuick
import QtQuick.Layouts
import Uzume.App

// Users › Devices (3m-app-1, 3m-app-2): "Waiting for approval" on top (device, system, asked time, Approve as, Approve, Deny) and
// "All devices" below (person, device, app, last seen, Sign out which revokes the device).
ColumnLayout {
    id: root
    required property var page
    spacing: Theme.space4

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space1
        UzSectionLabel { Layout.fillWidth: true; text: qsTr("Waiting for approval · %1").arg(root.page.requests.length) }
        Text {
            objectName: "devicesNothingWaiting"
            Layout.fillWidth: true
            visible: root.page.requests.length === 0
            topPadding: Theme.space1
            wrapMode: Text.Wrap
            text: qsTr("Nothing is waiting for approval. A new PC shows up here when it asks to join.")
            font.family: Theme.sans
            font.pixelSize: Theme.fontSmall
            color: Theme.muted
        }
        Repeater {
            model: root.page.requests
            delegate: ColumnLayout {
                id: req
                required property var modelData
                required property int index
                readonly property string target: root.page.approveChoice[modelData.id] ?? modelData.user_id ?? (root.page.userOptions.length > 0 ? root.page.userOptions[0].value : "")
                readonly property var asUser: root.page.userById(modelData.user_id ?? "")
                objectName: "request_" + modelData.id
                Layout.fillWidth: true
                spacing: Theme.space1
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.rule2 }
                Text {
                    objectName: "requestName_" + req.modelData.id
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space1
                    elide: Text.ElideRight
                    text: req.modelData.device_name
                    textFormat: Text.PlainText
                    font.family: Theme.sans
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Medium
                    color: Theme.ink
                }
                UzMono {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("%1 · %2 · app %3 · asked %4").arg(req.modelData.platform).arg(req.modelData.arch)
                              .arg(req.modelData.player_version).arg(root.page.timeText(req.modelData.created_at)).toUpperCase()
                }
                Text {
                    objectName: "requestText_" + req.modelData.id
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: req.asUser !== null ? qsTr("Asks to sign in as %1. Approve only if you know this PC.").arg(req.asUser.display_name)
                                              : qsTr("Asks to join. Choose who this PC belongs to. Approve only if you know this PC.")
                    textFormat: Text.PlainText
                    font.family: Theme.sans
                    font.pixelSize: Theme.fontSmall
                    color: Theme.ink2
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.bottomMargin: Theme.space1
                    spacing: Theme.space1
                    UzMono { text: qsTr("Approve as"); height: Theme.row36; verticalAlignment: Text.AlignVCenter }
                    UzSelect {
                        objectName: "approveAs_" + req.modelData.id
                        model: root.page.userOptions
                        current: req.target
                        onPicked: (value) => {
                            const c = Object.assign({}, root.page.approveChoice)
                            c[req.modelData.id] = value
                            root.page.approveChoice = c
                        }
                    }
                    UzButton {
                        objectName: "approve_" + req.modelData.id
                        kind: "primary"
                        text: qsTr("Approve")
                        enabled: req.target !== ""
                        onClicked: root.page.approve(req.modelData)
                    }
                    UzButton {
                        objectName: "deny_" + req.modelData.id
                        text: qsTr("Deny")
                        onClicked: root.page.deny(req.modelData)
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space1
        UzSectionLabel { Layout.fillWidth: true; text: qsTr("All devices · %1").arg(root.page.devices.length) }
        UzTable {
            objectPrefix: "devicesTable"
            Layout.fillWidth: true
            selectable: false
            emptyText: qsTr("No device has been paired yet.")
            columns: [
                { key: "person", title: qsTr("Person"), fill: 1 },
                { key: "device", title: qsTr("Device"), fill: 1.4 },
                { key: "app", title: qsTr("App"), width: 90, mono: true },
                { key: "seen", title: qsTr("Last seen"), width: 100, mono: true },
                { key: "action", title: "", width: 110, align: "right", custom: true }
            ]
            rows: root.page.deviceRows
            cellDelegate: Component {
                Item {
                    id: cell
                    property var row: null
                    property var column: null
                    implicitHeight: 32
                    UzButton {
                        objectName: "signOutDevice_" + (cell.row ? cell.row.id : "")
                        anchors.right: parent.right
                        visible: cell.row !== null && cell.row.trusted === true
                        size: "sm"
                        kind: "ghost"
                        text: qsTr("Sign out")
                        onClicked: root.page.signOutDevice(cell.row)
                    }
                }
            }
        }
    }
}
