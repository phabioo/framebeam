import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// Saves of all users (accounts-and-server.md 3s-app-1, 3s-app-4): a sheet over the window with the table of GET /admin/saves
// (person, game, slot, latest; tags Conflict and Removed; conflicts first, as the API sorts them) and, under the table, the
// details section of the selected save (SavesAdminDetails). A person who is no longer on the server is "Removed" (the id is not in
// GET /admin/users). Opened by the "All users" switch (SavesAdminSwitch); Esc or Close return to "Mine". Read again each time it opens.
// States: loading, error with Try again, empty ("No saves yet").
// objectNames: savesAdminSheet, savesAdminClose, savesAdminTable (prefix), savesAdminEmpty, savesAdminLoadError.
FocusScope {
    id: root
    objectName: "savesAdminSheet"
    required property PlayerController player
    readonly property AdminController admin: root.player.admin
    property bool shown: false
    property string phase: "idle"     // idle | loading | ready | error
    property var saves: []
    property int selected: -1
    readonly property var rows: root.saves.map(s => root.rowOf(s))
    readonly property var selectedSave: root.selected >= 0 && root.selected < root.saves.length ? root.saves[root.selected] : null

    parent: Window.contentItem
    anchors.fill: parent
    z: 100
    visible: shown
    focus: shown

    AdminCall { id: api; admin: root.admin }
    AdminPeople { id: directory; admin: root.admin }

    function open() {
        root.shown = true
        root.forceActiveFocus()
        directory.load()
        root.load("")
    }
    function close() { root.shown = false }
    Keys.onEscapePressed: (event) => { root.close(); event.accepted = true }

    function keyOf(s: var): string { return s.user_id + "/" + s.game_id + "/" + s.slot }
    function rowOf(s: var): var {
        const removed = directory.loaded && !directory.known(s.user_id)
        return {
            key: root.keyOf(s),
            person: directory.nameOf(s.user_id, s.username) + (removed ? " " + qsTr("(removed)") : ""),
            game: s.game_title,
            slot: s.slot,
            latest: qsTr("Rev %1 · %2").arg(s.current.revision).arg(root.whenText(s.current.created_at)),
            conflict: s.open_conflict_count > 0,
            removed: removed
        }
    }
    function whenText(iso: string): string {
        const d = new Date(iso)
        return isNaN(d.getTime()) ? "" : Qt.formatDateTime(d, "d MMM, HH:mm")
    }
    // `selectKey`: the save to select after the list was read ("" = keep the selection when it still exists).
    function load(selectKey: string) {
        const keep = selectKey !== "" ? selectKey : (root.selectedSave ? root.keyOf(root.selectedSave) : "")
        if (root.saves.length === 0) root.phase = "loading"
        api.call("GET", "/admin/saves", undefined, function (ok, data) {
            if (!ok || !data || data.saves === undefined) { root.phase = "error"; return }
            root.saves = data.saves
            root.phase = "ready"
            let at = -1
            for (let i = 0; i < data.saves.length; ++i) if (root.keyOf(data.saves[i]) === keep) at = i
            root.selected = at
        })
    }

    Rectangle {   // the scrim: ground at 80 %, swallows clicks
        anchors.fill: parent
        color: Theme.scrim
        MouseArea { anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.AllButtons; onWheel: (wheel) => wheel.accepted = true; onClicked: root.close() }
    }
    Rectangle {
        id: frame
        anchors.centerIn: parent
        width: Math.min(980, Math.max(0, root.width - 2 * Theme.space3))
        height: Math.max(0, root.height - 2 * Theme.space3)
        color: Theme.surface
        border.width: 1
        border.color: Theme.rule
        clip: true
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.space3
            spacing: Theme.space2
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space1
                    UzMono { text: qsTr("Library · Saves").toUpperCase() }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        text: qsTr("Saves of all users")
                        font.family: Theme.sans
                        font.pixelSize: 20
                        font.weight: Font.Medium
                        color: Theme.ink
                    }
                }
                UzButton { objectName: "savesAdminClose"; kind: "ghost"; text: qsTr("Close"); onClicked: root.close() }
            }
            Flickable {
                id: scroller
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: body.implicitHeight + Theme.space3
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: body
                    width: scroller.width - Theme.space2
                    spacing: Theme.space3
                    UzStateBlock {
                        objectName: "savesAdminLoading"
                        Layout.fillWidth: true
                        visible: root.phase === "loading"
                        kind: "loading"
                        title: qsTr("Reading the saves")
                    }
                    UzStateBlock {
                        objectName: "savesAdminLoadError"
                        Layout.fillWidth: true
                        visible: root.phase === "error"
                        kind: "error"
                        title: qsTr("Could not read the saves")
                        text: qsTr("The server did not answer.")
                        action: qsTr("Try again")
                        onActionClicked: root.load("")
                    }
                    UzTable {
                        id: table
                        objectPrefix: "savesAdminTable"
                        Layout.fillWidth: true
                        visible: root.phase === "ready"
                        emptyText: qsTr("No saves yet. Saves appear here when someone plays.")
                        columns: [
                            { key: "person", title: qsTr("Person"), fill: 1 },
                            { key: "game", title: qsTr("Game"), fill: 1.5 },
                            { key: "slot", title: qsTr("Slot"), width: 90, mono: true },
                            { key: "latest", title: qsTr("Latest"), width: 170, mono: true },
                            { key: "tags", title: "", width: 150, align: "right", custom: true }
                        ]
                        rows: root.rows
                        currentIndex: root.selected
                        onPicked: (i, row) => root.selected = i
                        cellDelegate: Component {
                            Row {
                                id: cell
                                property var row: null
                                property var column: null
                                layoutDirection: Qt.RightToLeft
                                spacing: Theme.space1
                                UzTag { visible: cell.row !== null && cell.row.conflict === true; objectName: "savesAdminTagConflict"; tone: "warn"; variant: "plane"; text: qsTr("Conflict") }
                                UzTag { visible: cell.row !== null && cell.row.removed === true; objectName: "savesAdminTagRemoved"; text: qsTr("Removed") }
                            }
                        }
                    }
                    SavesAdminDetails {
                        id: details
                        objectName: "savesAdminDetails"
                        Layout.fillWidth: true
                        visible: root.phase === "ready" && root.selectedSave !== null
                        player: root.player
                        people: directory
                        save: root.selectedSave
                        onChanged: root.load("")
                        onSlotMoved: (userId, gameId, slot) => root.load(slot === "" ? "" : userId + "/" + gameId + "/" + slot)
                    }
                }
            }
        }
    }
}
