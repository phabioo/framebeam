import QtQuick
import QtQuick.Layouts
import Uzume.App

// Device approval (8b-5g, 5h, 5i): the device chip, the waiting state with the ensō and Cancel request, and the declined /
// expired state with Ask again. `state`: "ask" | "wait" | "denied" | "expired".
ColumnLayout {
    id: root
    property string prefix: "approval"
    property string mode: "wait"            // ask | wait | denied | expired
    property string deviceName: ""
    property string platformText: ""
    property string serverName: ""
    signal ask()
    signal cancel()
    signal back()
    spacing: Theme.space2
    Layout.fillWidth: true

    Rectangle {   // device chip: NAME · PLATFORM · NEW DEVICE
        objectName: root.prefix + "DeviceChip"
        Layout.preferredHeight: 28
        Layout.preferredWidth: chipText.implicitWidth + 2 * Theme.space15
        color: "transparent"
        border.width: 1
        border.color: Theme.rule
        Text {
            id: chipText
            anchors.centerIn: parent
            text: root.deviceName + (root.platformText !== "" ? " · " + root.platformText : "") + " · " + qsTr("New device")
            textFormat: Text.PlainText
            font.family: Theme.mono
            font.pixelSize: Theme.roles.meta.size
            font.letterSpacing: Theme.roles.meta.spacing
            font.capitalization: Font.AllUppercase
            color: Theme.ink2
        }
    }
    RowLayout {
        visible: root.mode === "wait"
        Layout.fillWidth: true
        spacing: Theme.space2
        UzEnso { objectName: root.prefix + "Enso"; size: 56 }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space05
            Text {
                text: qsTr("Waiting for an admin")
                textFormat: Text.PlainText
                font.family: Theme.sans
                font.pixelSize: Theme.roles.itemTitle.size
                font.weight: Font.Medium
                color: Theme.ink
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("An admin sees your request in Users › Devices and gets a notification.")
                textFormat: Text.PlainText
                font.family: Theme.sans
                font.pixelSize: Theme.roles.small.size
                color: Theme.muted
            }
        }
    }
    UzStatusIcon {
        objectName: root.prefix + "Declined"
        visible: root.mode === "denied" || root.mode === "expired"
        kind: root.mode === "denied" ? "error" : "warning"
        text: root.mode === "denied" ? qsTr("%1 was not approved.").arg(root.deviceName) : qsTr("The request ran out before an admin answered.")
        maxWidth: 480
    }
    Text {
        visible: root.mode === "denied"
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: qsTr("You can ask again after 10 minutes.")
        textFormat: Text.PlainText
        font.family: Theme.sans
        font.pixelSize: Theme.roles.small.size
        color: Theme.muted
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space1
        UzButton {
            objectName: root.prefix + "Ask"
            visible: root.mode !== "wait"
            kind: "primary"
            text: root.mode === "ask" ? qsTr("Ask for approval") : qsTr("Ask again")
            onClicked: root.ask()
        }
        UzButton {
            objectName: root.prefix + "Cancel"
            visible: root.mode === "wait"
            text: qsTr("Cancel request")
            onClicked: root.cancel()
        }
        UzButton {
            objectName: root.prefix + "Back"
            visible: root.mode !== "wait"
            text: qsTr("Back")
            onClicked: root.back()
        }
        Item { Layout.fillWidth: true }
    }
}
