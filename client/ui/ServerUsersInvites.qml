import QtQuick
import QtQuick.Layouts
import Uzume.App

// Users › Invites (3n-app-2, 3n-app-3 "no open invites"): invites and pairing codes, newest first, with Withdraw for open ones.
// The server never lists the codes, so the table shows what the API carries: kind, whether the first PC is trusted at once,
// status, expiry and who used it.
ColumnLayout {
    id: root
    required property var page
    spacing: Theme.space2

    UzStateBlock {
        objectName: "invitesEmpty"
        Layout.fillWidth: true
        visible: root.page.invites.length === 0
        kind: "empty"
        title: qsTr("No open invites")
        text: qsTr("Invite a friend with a code. It works until it expires and can be withdrawn.")
        action: qsTr("Create invite…")
        onActionClicked: root.page.openInviteDialog()
    }
    Text {
        objectName: "invitesNoneOpen"
        Layout.fillWidth: true
        visible: root.page.invites.length > 0 && root.page.openInvites === 0
        wrapMode: Text.Wrap
        text: qsTr("No invite is open right now.")
        font.family: Theme.sans
        font.pixelSize: Theme.roles.input.size
        color: Theme.muted
    }
    UzTable {
        objectPrefix: "invitesTable"
        Layout.fillWidth: true
        visible: root.page.invites.length > 0
        selectable: false
        columns: [
            { key: "kind", title: qsTr("Invite"), fill: 1.3 },
            { key: "trust", title: qsTr("First PC"), fill: 1 },
            { key: "status", title: qsTr("Status"), width: 100 },
            { key: "expires", title: qsTr("Expires"), width: 80, mono: true },
            { key: "usedBy", title: qsTr("Used by"), fill: 1 },
            { key: "action", title: "", width: 110, align: "right", custom: true }
        ]
        rows: root.page.inviteRows
        cellDelegate: Component {
            Item {
                id: cell
                property var row: null
                property var column: null
                implicitHeight: 32
                UzButton {
                    objectName: "withdrawInvite_" + (cell.row ? cell.row.id : "")
                    anchors.right: parent.right
                    visible: cell.row !== null && cell.row.open === true
                    size: "sm"
                    kind: "ghost"
                    text: qsTr("Withdraw")
                    onClicked: root.page.withdrawInvite(cell.row)
                }
            }
        }
    }
}
