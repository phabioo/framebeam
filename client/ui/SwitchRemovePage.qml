import QtQuick
import QtQuick.Layouts
import Uzume.App

// 8d-7a remove from this PC (nothing is deleted on the server, all saves are uploaded; the person can sign in again) and
// 8d-7b the saves that are not uploaded yet (wait, or remove anyway). `blocked` selects the second.
StartPage {
    id: root
    required property PlayerController player
    property bool blocked: false
    readonly property var ac: player.accounts
    screenLabel: qsTr("Switch user · remove")
    openLabel: qsTr("UZUME · SWITCH USER")
    initialFocus: blocked ? wait : cancel
    hints: [{ kbd: "Enter", pad: "A", label: qsTr("Choose") }, { kbd: "Esc", pad: "B", label: qsTr("Back") }]
    onBackRequested: ac.open()

    StartHead {
        number: 1
        label: qsTr("Switch user")
        title: root.blocked ? qsTr("%1 isn't uploaded yet").arg(Theme.plural(root.ac.pendingCount, qsTr("save"), qsTr("saves")))
                            : qsTr("Remove %1 from this PC?").arg(root.ac.targetName)
        text: root.blocked ? qsTr("%1 has saves on this PC that the server doesn't have yet (the server may be offline). Wait until they are uploaded, or let %1 sign in so they upload.").arg(root.ac.targetName)
                           : qsTr("Nothing is deleted on the server and all saves are uploaded. %1 can sign in again at any time.").arg(root.ac.targetName)
    }
    UzStatusIcon {
        objectName: "removeError"
        visible: root.ac.errorMessage !== ""
        kind: "error"
        small: true
        text: root.ac.errorMessage
        maxWidth: 480
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space1
        UzButton { id: wait; objectName: "removeWait"; visible: root.blocked; kind: "primary"; text: qsTr("Wait"); onClicked: root.ac.open() }
        UzButton { id: cancel; objectName: "removeCancel"; visible: !root.blocked; kind: "primary"; text: qsTr("Cancel"); onClicked: root.ac.open() }
        UzButton { objectName: "removeConfirm"; visible: !root.blocked; kind: "danger"; busy: root.ac.busy; text: qsTr("Remove from this PC"); onClicked: root.ac.confirmRemove() }
        UzButton { objectName: "removeAnyway"; visible: root.blocked; kind: "danger"; text: qsTr("Remove anyway"); onClicked: root.ac.confirmRemoveAnyway() }
        Item { Layout.fillWidth: true }
    }
}
