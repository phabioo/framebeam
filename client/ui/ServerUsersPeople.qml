import QtQuick
import QtQuick.Layouts
import Uzume.App

// Users › People (3n-app-1, 3n-app-3): table person (since), role, status, devices; the selected person's details section under
// the table with the actions Make admin / Make member, Reset password, Turn off / Turn on, Remove and a new pairing code.
// Admins cannot be turned off or removed (the server refuses); the reason is shown next to the disabled action.
ColumnLayout {
    id: root
    required property var page
    readonly property var u: root.page.selected
    readonly property bool isSelf: root.page.isSelf(root.u)
    readonly property bool isAdmin: root.u !== null && root.u.role === "admin"
    readonly property bool soleAdmin: root.isAdmin && root.page.adminCount <= 1
    readonly property string name: root.u !== null ? root.u.display_name : ""
    // Why an action is not available (empty = available).
    readonly property string offReason: root.u === null ? ""
        : root.soleAdmin ? qsTr("Make someone else admin first.")
        : root.isAdmin ? qsTr("Admins can't be turned off. Make %1 a member first.").arg(root.name) : ""
    readonly property string roleReason: root.u === null ? ""
        : root.isAdmin && root.isSelf ? qsTr("You can't change your own role.")
        : root.soleAdmin ? qsTr("Make someone else admin first.")
        : !root.isAdmin && root.u.disabled ? qsTr("Turn %1 on first.").arg(root.name) : ""
    spacing: Theme.space2

    UzTable {
        id: table
        objectPrefix: "peopleTable"
        Layout.fillWidth: true
        emptyText: qsTr("Nobody is here yet.")
        columns: [
            { key: "person", title: qsTr("Person"), fill: 1.4, custom: true },
            { key: "role", title: qsTr("Role"), width: 90 },
            { key: "status", title: qsTr("Status"), fill: 1 },
            { key: "devices", title: qsTr("Devices"), width: 80, align: "right", mono: true }
        ]
        rows: root.page.peopleRows
        currentIndex: root.page.selectedIndex
        onPicked: (i, row) => root.page.selectedId = row.id
        cellDelegate: Component {
            Item {
                id: cell
                property var row: null
                property var column: null
                implicitHeight: col.implicitHeight
                Column {
                    id: col
                    width: parent.width
                    Text {
                        objectName: "personName_" + (cell.row ? cell.row.id : "")
                        width: parent.width
                        elide: Text.ElideRight
                        text: cell.row ? cell.row.person : ""
                        textFormat: Text.PlainText
                        font.family: Theme.sans
                        font.pixelSize: Theme.fontBody
                        color: Theme.ink
                    }
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: cell.row ? cell.row.since : ""
                        textFormat: Text.PlainText
                        font.family: Theme.mono
                        font.pixelSize: Theme.fontMeta
                        color: Theme.muted
                    }
                }
            }
        }

        ColumnLayout {   // details section of the selected person
            objectName: "peopleDetails"
            width: parent ? parent.width : 0
            visible: root.u !== null
            spacing: Theme.space2
            Item { Layout.preferredHeight: Theme.space2 }
            UzSectionLabel { Layout.fillWidth: true; Layout.leftMargin: Theme.space15; Layout.rightMargin: Theme.space15; text: root.name }
            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space15
                Layout.rightMargin: Theme.space15
                columns: 2
                columnSpacing: Theme.space3
                rowSpacing: Theme.space1
                Text { text: qsTr("Role"); font.family: Theme.sans; font.pixelSize: Theme.fontSmall; color: Theme.muted }
                Text { objectName: "detailRole"; text: root.isAdmin ? qsTr("Admin") : qsTr("Member"); font.family: Theme.sans; font.pixelSize: Theme.fontBody; color: Theme.ink }
                Text { text: qsTr("Last seen"); font.family: Theme.sans; font.pixelSize: Theme.fontSmall; color: Theme.muted }
                Text {
                    objectName: "detailLastSeen"
                    text: root.u === null ? "" : root.u.last_seen_at ? root.page.agoText(root.u.last_seen_at) : qsTr("Never")
                    font.family: Theme.sans; font.pixelSize: Theme.fontBody; color: Theme.ink
                }
                Text { text: qsTr("Joined"); font.family: Theme.sans; font.pixelSize: Theme.fontSmall; color: Theme.muted }
                Text {
                    text: root.u === null ? "" : root.page.dateText(root.u.created_at)
                    font.family: Theme.sans; font.pixelSize: Theme.fontBody; color: Theme.ink
                }
                Text { text: qsTr("Devices"); font.family: Theme.sans; font.pixelSize: Theme.fontSmall; color: Theme.muted }
                Text {
                    objectName: "detailDevices"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: {
                        if (root.u === null) return ""
                        const names = root.page.devicesOf(root.u)
                        return names.length > 0 ? names.join(", ") : qsTr("None")
                    }
                    textFormat: Text.PlainText
                    font.family: Theme.sans; font.pixelSize: Theme.fontBody; color: Theme.ink
                }
            }
            Flow {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space15
                Layout.rightMargin: Theme.space15
                spacing: Theme.space1
                UzButton {
                    objectName: "personMakeAdmin"
                    visible: !root.isAdmin
                    text: qsTr("Make admin…")
                    enabled: root.roleReason === ""
                    onClicked: root.page.makeAdmin(root.u)
                }
                UzButton {
                    objectName: "personMakeMember"
                    visible: root.isAdmin
                    text: qsTr("Make member…")
                    enabled: root.roleReason === ""
                    onClicked: root.page.makeMember(root.u)
                }
                UzButton {
                    objectName: "personResetPassword"
                    text: qsTr("Reset password…")
                    enabled: !root.isSelf
                    onClicked: root.page.resetPassword(root.u)
                }
                UzButton {
                    objectName: "personPairingCode"
                    visible: !root.isAdmin
                    text: qsTr("New pairing code…")
                    enabled: root.u !== null && !root.u.disabled
                    onClicked: root.page.newPairingCode(root.u)
                }
                UzButton {
                    objectName: "personTurnOff"
                    visible: root.u !== null && !root.u.disabled
                    text: qsTr("Turn off…")
                    enabled: root.offReason === ""
                    onClicked: root.page.turnOff(root.u)
                }
                UzButton {
                    objectName: "personTurnOn"
                    visible: root.u !== null && root.u.disabled
                    text: qsTr("Turn on")
                    onClicked: root.page.turnOn(root.u, false)
                }
                UzButton {
                    objectName: "personRemove"
                    kind: "danger"
                    visible: !root.isAdmin
                    text: qsTr("Remove…")
                    onClicked: root.page.removeUser(root.u)
                }
            }
            UzStatusIcon {
                objectName: "personHint"
                Layout.leftMargin: Theme.space15
                Layout.rightMargin: Theme.space15
                visible: text !== ""
                kind: "warning"
                small: true
                text: root.offReason !== "" ? root.offReason : root.roleReason
                maxWidth: 560
            }
            Item { Layout.preferredHeight: Theme.space2 }
        }
    }
}
