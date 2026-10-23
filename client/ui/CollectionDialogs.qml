import QtQuick
import QtQuick.Layouts
import Uzume.App

// The dialogs of favourites and collections (board S3c4): "New collection" / "Rename collection", "Delete collection" and
// "Add to collection" (a checkbox per collection, a New collection step). They cover the whole window; the requests go through
// `player.personal`, which takes a change back when the server refuses it. A collection holds nothing but the person's own list of
// games: deleting it never deletes games.
//   openNew(gameId)   gameId (optional): the new collection takes this game at once
//   openRename(collectionId), openDelete(collectionId), openAdd(gameId)
// objectNames: collectionNameDialog, collectionNameField, collectionNameConfirm, collectionNameCancel, collectionDeleteDialog,
// collectionDeleteConfirm, collectionAddDialog, collectionAddNew, collectionAddDone, collectionCheck_<id>.
Item {
    id: root
    required property PlayerController player
    readonly property PersonalController personal: player.personal
    // The shown dialog of this instance asked for the request that is running (another instance ignores its answer).
    property string waitingFor: ""
    property string collectionId: ""
    property string gameId: ""

    function openNew(forGame: string) {
        root.collectionId = ""
        root.gameId = forGame ?? ""
        nameField.text = ""
        nameDialog.error = ""
        nameDialog.open()
        nameField.forceActiveFocus()
    }
    function openRename(id: string) {
        root.collectionId = id
        root.gameId = ""
        nameField.text = root.personal.collectionName(id)
        nameDialog.error = ""
        nameDialog.open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }
    function openDelete(id: string) {
        root.collectionId = id
        deleteDialog.error = ""
        deleteDialog.open()
    }
    function openAdd(forGame: string) {
        root.gameId = forGame
        addDialog.error = ""
        addDialog.open()
    }
    function submitName() {
        if (nameDialog.busy) return
        const problem = root.personal.nameProblem(nameField.text, root.collectionId)
        if (problem !== "") { nameDialog.error = problem; return }
        nameDialog.error = ""
        nameDialog.busy = true
        if (root.collectionId === "") {
            root.waitingFor = "create"
            root.personal.createCollection(nameField.text, root.gameId)
        } else {
            root.waitingFor = "rename"
            root.personal.renameCollection(root.collectionId, nameField.text)
        }
    }

    Connections {
        target: root.personal
        function onFinished(op, ok, error) {
            if (op === "member" && addDialog.shown) { addDialog.error = ok ? "" : error; return }
            if (op !== root.waitingFor) return
            root.waitingFor = ""
            if (op === "create" || op === "rename") {
                nameDialog.busy = false
                if (ok) nameDialog.close()
                else nameDialog.error = error
            } else if (op === "delete") {
                deleteDialog.busy = false
                if (ok) deleteDialog.close()
                else deleteDialog.error = error
            }
        }
    }

    UzDialog {
        id: nameDialog
        objectName: "collectionNameDialog"
        z: 101   // above the "Add to collection" dialog it can be opened from
        parent: Window.contentItem
        label: qsTr("Library")
        title: root.collectionId === "" ? qsTr("New collection") : qsTr("Rename collection")
        text: root.collectionId === "" ? qsTr("A collection is your own list of games. Only you see it, on every PC.")
                                       : qsTr("Only you see this collection.")
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space05
            UzMono { text: qsTr("Name").toUpperCase() }
            UzField {
                id: nameField
                objectName: "collectionNameField"
                Layout.fillWidth: true
                fill: Theme.ground
                maximumLength: 40
                placeholderText: qsTr("Couch co-op")
                Accessible.name: qsTr("Name")
                onAccepted: root.submitName()
            }
        }
        actions: [
            UzButton { objectName: "collectionNameCancel"; text: qsTr("Cancel"); enabled: !nameDialog.busy; onClicked: nameDialog.close() },
            UzButton {
                objectName: "collectionNameConfirm"
                kind: "primary"
                text: root.collectionId === "" ? qsTr("Create") : qsTr("Rename")
                enabled: nameField.text.trim() !== ""
                busy: nameDialog.busy
                onClicked: root.submitName()
            }
        ]
    }

    UzDialog {
        id: deleteDialog
        objectName: "collectionDeleteDialog"
        z: 101
        parent: Window.contentItem
        label: qsTr("Library")
        title: qsTr("Delete collection “%1”?").arg(root.personal.collectionName(root.collectionId))
        text: qsTr("Deleting a collection never deletes games.")
        actions: [
            UzButton { objectName: "collectionDeleteCancel"; text: qsTr("Cancel"); enabled: !deleteDialog.busy; onClicked: deleteDialog.close() }
        ]
        dangerActions: [
            UzButton {
                objectName: "collectionDeleteConfirm"
                kind: "danger"
                text: qsTr("Delete collection")
                busy: deleteDialog.busy
                onClicked: {
                    deleteDialog.error = ""
                    deleteDialog.busy = true
                    root.waitingFor = "delete"
                    root.personal.deleteCollection(root.collectionId)
                }
            }
        ]
    }

    UzDialog {
        id: addDialog
        objectName: "collectionAddDialog"
        parent: Window.contentItem
        label: qsTr("Game")
        title: qsTr("Add to collection")
        // Rebuilt when a membership or the list of collections changes.
        readonly property var chips: root.player.library.collectionChips
        readonly property var mine: { addDialog.chips; return root.player.library.personalFor(root.gameId).collectionIds ?? [] }
        readonly property var collections: chips.filter(c => c.kind === "collection")
        text: collections.length === 0 ? qsTr("You have no collections yet.") : ""
        Repeater {
            model: addDialog.collections
            delegate: UzCheckbox {
                required property var modelData
                objectName: "collectionCheck_" + modelData.value
                Layout.fillWidth: true
                text: modelData.label
                checked: addDialog.mine.indexOf(modelData.value) >= 0
                onToggled: {
                    root.personal.setInCollection(modelData.value, root.gameId, checked)
                    checked = Qt.binding(() => addDialog.mine.indexOf(modelData.value) >= 0)
                }
            }
        }
        actions: [
            UzButton { objectName: "collectionAddNew"; text: qsTr("+ New collection"); onClicked: root.openNew(root.gameId) },
            UzButton { objectName: "collectionAddDone"; kind: "primary"; text: qsTr("Done"); onClicked: addDialog.close() }
        ]
    }
}
