import QtQuick
import QtQuick.Layouts
import Uzume.App

// Saves of all users, details section of the selected save (accounts-and-server.md 3s-app-1, 3s-app-2, 3s-app-4). Everything goes
// through /admin/users/{user}/games/{game}/saves/{slot}/...: the slot with its open conflicts, the history (newest first),
// "Keep this" per version of a conflict (resolve), Restore for another person (the current version stays in the history as
// "Before restore"), download of a version, Keep as snapshot, New slot from this, Rename / Delete slot and "Upload save file"
// for a person (the current version stays in the history as "Before upload"; the expected revision makes a stale upload fail
// instead of overwriting). The server asks "Confirm it's you" itself. Destructive steps ask first; every step shows its outcome.
// `changed()` tells the list to read again; `slotMoved(userId, gameId, slot)` after a rename or a new slot.
// objectNames: savesAdminDetails, savesAdminConflict, savesAdminKeepHub, savesAdminKeepLocal, savesAdminHistory,
// savesAdminRestore_<v>, savesAdminDownload_<v>, savesAdminDeleteVersion_<v>, savesAdminSnapshot, savesAdminNewSlot,
// savesAdminRename, savesAdminDeleteSlot, savesAdminUpload, savesAdminDownloadLatest, savesAdminNote.
Item {
    id: root
    implicitHeight: col.implicitHeight
    objectName: "savesAdminDetails"
    required property PlayerController player
    required property AdminPeople people
    property var save: null          // a row of GET /admin/saves
    property var slotData: null      // GET .../saves/{slot}
    property var versions: []        // GET .../history
    property string phase: "idle"    // idle | loading | ready | error
    property string note: ""
    property string noteKind: "done"
    property int pendingReads: 0
    signal changed()
    signal slotMoved(string userId, string gameId, string slot)

    readonly property AdminController admin: root.player.admin
    readonly property string base: root.save ? "/admin/users/" + encodeURIComponent(root.save.user_id) + "/games/" + encodeURIComponent(root.save.game_id)
                                               + "/saves/" + encodeURIComponent(root.save.slot) : ""
    readonly property string who: root.save ? root.people.nameOf(root.save.user_id, root.save.username) : ""
    readonly property var current: root.slotData && root.slotData.current ? root.slotData.current : (root.save ? root.save.current : null)
    readonly property var conflict: root.slotData && root.slotData.open_conflicts && root.slotData.open_conflicts.length > 0 ? root.slotData.open_conflicts[0] : null
    readonly property bool isDefault: root.save !== null && root.save.slot === "default"

    AdminCall { id: api; admin: root.admin }
    AdminAskDialog { id: ask }

    // ------------------------------------------------------------ formatting
    function whenText(iso: string): string {
        const d = new Date(iso)
        return isNaN(d.getTime()) ? "" : Qt.formatDateTime(d, "d MMM yyyy, HH:mm")
    }
    function sizeText(n: real): string {
        const mib = 1024 * 1024
        return n >= mib ? qsTr("%1 MB").arg((n / mib).toFixed(1)) : qsTr("%1 KB").arg(Math.max(1, Math.round(n / 1024)))
    }
    function kindText(v: var): string {
        let t
        switch (v.reason) {
        case "session_end": t = qsTr("End of a Session"); break
        case "device_change": t = qsTr("Device changed"); break
        case "before_conflict_resolution": t = qsTr("Before conflict resolution"); break
        case "conflict_upload": t = qsTr("Upload in conflict"); break
        case "manual_snapshot": t = qsTr("Snapshot"); break
        case "before_restore": t = qsTr("Before restore"); break
        case "before_upload": t = qsTr("Before upload"); break
        default: t = v.reason
        }
        return v.label ? t + " · " + v.label : t
    }
    function say(text: string, kind: string) {
        root.note = text
        root.noteKind = kind
    }
    function failed(what: string, code: string, message: string): string {
        if (code === "confirmation_cancelled") return qsTr("Not confirmed. Nothing was changed.")
        return api.failText(what, message)
    }

    // ------------------------------------------------------------ data
    function load() {
        root.slotData = null
        root.versions = []
        root.note = ""
        if (!root.save) { root.phase = "idle"; return }
        root.phase = "loading"
        const key = root.base
        root.pendingReads = 2
        let bad = false
        const done = () => {
            if (key !== root.base) return
            root.pendingReads -= 1
            if (root.pendingReads === 0) root.phase = bad ? "error" : "ready"
        }
        api.call("GET", key, undefined, function (ok, data) {
            if (key !== root.base) return
            if (ok && data && data.current !== undefined) root.slotData = data; else bad = true
            done()
        })
        api.call("GET", key + "/history", undefined, function (ok, data) {
            if (key !== root.base) return
            if (ok && data && data.versions !== undefined) root.versions = data.versions; else bad = true
            done()
        })
    }
    onBaseChanged: root.load()
    Component.onCompleted: root.load()

    function afterChange(text: string) {
        root.load()
        root.say(text, "done")
        root.changed()
    }

    // ------------------------------------------------------------ conflict
    function keep(side: string) {   // "hub" | "local"
        const c = root.conflict
        if (!c) return
        const text = side === "hub" ? qsTr("Rev %1 from %2").arg(c.hub.revision).arg(c.hub.device_name)
                                    : qsTr("v%1 from %2").arg(c.secured.version).arg(c.secured.device_name)
        ask.ask({
            label: qsTr("Saves"),
            title: qsTr("Keep %1?").arg(text),
            text: qsTr("It becomes the newest version of %1's save. The other one stays in the history.").arg(root.who),
            action: qsTr("Keep this"),
            run: (value, finish) => api.call("POST", root.base + "/conflicts/" + encodeURIComponent(c.id) + "/resolve",
                                            { resolution: side === "hub" ? "use_hub" : "use_local", expected_revision: root.current.revision },
                                            function (ok, data, code, message) {
                if (ok) root.afterChange(qsTr("Kept %1").arg(text))
                finish(ok, ok ? "" : root.failed(qsTr("Could not resolve the conflict"), code, message))
            })
        })
    }

    // ------------------------------------------------------------ history
    function restore(v: var) {
        ask.ask({
            label: qsTr("Saves"),
            title: qsTr("Restore v%1 for %2?").arg(v.version).arg(root.who),
            text: qsTr("v%1 becomes the newest version of %2's save. The current version stays in the history as “Before restore”. If %2 is playing this game right now, ask them to close it first.")
                      .arg(v.version).arg(root.who),
            action: qsTr("Restore v%1").arg(v.version),
            run: (value, finish) => api.call("POST", root.base + "/history/" + v.version + "/restore",
                                            { expected_revision: root.current.revision }, function (ok, data, code, message) {
                if (ok) root.afterChange(qsTr("Restored v%1 for %2").arg(v.version).arg(root.who))
                finish(ok, ok ? "" : root.failed(qsTr("Could not restore"), code, message))
            })
        })
    }
    function deleteVersion(v: var) {
        ask.ask({
            label: qsTr("Saves"),
            title: qsTr("Delete v%1?").arg(v.version),
            text: qsTr("This version of %1's save is removed from the history for good.").arg(root.who),
            action: qsTr("Delete v%1").arg(v.version),
            danger: true,
            run: (value, finish) => api.call("DELETE", root.base + "/history/" + v.version, undefined, function (ok, data, code, message) {
                if (ok) root.afterChange(qsTr("Deleted v%1").arg(v.version))
                finish(ok, ok ? "" : root.failed(qsTr("Could not delete"), code, message))
            })
        })
    }
    function suggestedName(version: int): string {
        const clean = (s) => String(s).replace(/[^A-Za-z0-9._-]+/g, "-").replace(/^-+|-+$/g, "")
        return clean(root.save.game_title) + "-" + clean(root.who) + "-" + root.save.slot + (version > 0 ? "-v" + version : "") + ".sav"
    }
    property string downloadPath: ""
    property string downloadLabel: ""
    function download(version: int) {   // 0 = the latest version
        root.downloadPath = version > 0 ? root.base + "/history/" + version + "/content" : root.base + "/content"
        root.downloadLabel = version > 0 ? qsTr("v%1").arg(version) : qsTr("the latest version")
        saveDialog.open(root.suggestedName(version))
    }
    function saveTo(pathOrUrl: string) {
        const label = root.downloadLabel
        api.download(root.downloadPath, pathOrUrl, function (ok, filePath, message) {
            root.say(ok ? qsTr("Saved %1 to %2").arg(label).arg(filePath) : api.failText(qsTr("Could not save the file"), message), ok ? "done" : "error")
        })
    }

    // ------------------------------------------------------------ slot
    function validSlot(name: string): bool { return /^[a-z0-9_-]{1,32}$/.test(name) }
    readonly property string slotRule: qsTr("Use 1 to 32 characters: a to z, 0 to 9, - and _.")
    function snapshot() {
        ask.ask({
            label: qsTr("Saves"), title: qsTr("Keep the current version as a snapshot?"),
            text: qsTr("It stays in the history of %1's save and is never removed automatically.").arg(root.who),
            action: qsTr("Keep snapshot"), input: true, inputOptional: true, inputLabel: qsTr("Label (optional)"), maxLength: 64,
            run: (value, finish) => api.call("POST", root.base + "/snapshots", value !== "" ? { label: value } : undefined, function (ok, data, code, message) {
                if (ok) root.afterChange(qsTr("Kept a snapshot"))
                finish(ok, ok ? "" : root.failed(qsTr("Could not keep a snapshot"), code, message))
            })
        })
    }
    function newSlot() {
        ask.ask({
            label: qsTr("Saves"), title: qsTr("New slot from this save"),
            text: qsTr("Copies the current version into a new slot of %1. This slot does not change.").arg(root.who),
            action: qsTr("Create slot"), input: true, inputLabel: qsTr("Slot name"), maxLength: 32,
            run: (value, finish) => {
                if (!root.validSlot(value)) { finish(false, root.slotRule); return }
                api.call("POST", root.base + "/slots", { name: value }, function (ok, data, code, message) {
                    if (ok) { root.say(qsTr("Created slot %1").arg(value), "done"); root.changed(); root.slotMoved(root.save.user_id, root.save.game_id, value) }
                    finish(ok, ok ? "" : root.failed(qsTr("Could not create the slot"), code, message))
                })
            }
        })
    }
    function renameSlot() {
        ask.ask({
            label: qsTr("Saves"), title: qsTr("Rename slot %1").arg(root.save.slot),
            text: qsTr("The history moves with it."),
            action: qsTr("Rename"), input: true, inputLabel: qsTr("New name"), inputText: root.save.slot, maxLength: 32,
            run: (value, finish) => {
                if (!root.validSlot(value)) { finish(false, root.slotRule); return }
                api.call("POST", root.base + "/rename", { name: value }, function (ok, data, code, message) {
                    if (ok) { root.say(qsTr("Renamed to %1").arg(value), "done"); root.changed(); root.slotMoved(root.save.user_id, root.save.game_id, value) }
                    finish(ok, ok ? "" : root.failed(qsTr("Could not rename"), code, message))
                })
            }
        })
    }
    function deleteSlot() {
        ask.ask({
            label: qsTr("Saves"), title: qsTr("Delete slot %1 of %2?").arg(root.save.slot).arg(root.who),
            text: qsTr("The slot and its whole history are deleted for good, for %1 and on every device. There is no undo.").arg(root.who),
            action: qsTr("Delete slot"), danger: true,
            run: (value, finish) => api.call("DELETE", root.base + "?confirm_revision=" + root.current.revision, undefined, function (ok, data, code, message) {
                if (ok) { root.say(qsTr("Deleted slot %1 of %2").arg(root.save.slot).arg(root.who), "done"); root.changed(); root.slotMoved("", "", "") }
                finish(ok, ok ? "" : root.failed(qsTr("Could not delete the slot"), code, message))
            })
        })
    }

    // ------------------------------------------------------------ upload
    readonly property int uploadLimit: 64 * 1024 * 1024
    function stageUpload(fileUrl: string) {
        const name = String(fileUrl).split("/").pop()
        const size = root.admin.sizeOfFile(fileUrl)
        if (size < 0) { root.say(qsTr("Could not read %1.").arg(name), "error"); return }
        if (size > root.uploadLimit) { root.say(qsTr("%1 is larger than 64 MB. Save files can be 64 MB at most.").arg(name), "error"); return }
        const rev = root.current ? root.current.revision : 0
        ask.ask({
            label: qsTr("Saves"),
            title: qsTr("Upload %1 for %2?").arg(name).arg(root.who),
            text: qsTr("%1 (%2) becomes the newest version of %3's slot %4. The current version (Rev %5) stays in the history as “Before upload”; nothing is deleted.")
                      .arg(name).arg(root.sizeText(size)).arg(root.who).arg(root.save.slot).arg(rev),
            action: qsTr("Upload"),
            run: (value, finish) => api.upload("POST", root.base + "/upload", fileUrl, "application/octet-stream",
                                               { "X-Uzume-Expected-Revision": String(rev), "X-Uzume-Content-SHA256": root.admin.sha256OfFile(fileUrl) },
                                               function (ok, data, code, message) {
                if (ok) root.afterChange(qsTr("Uploaded %1 for %2. The old version is in the history.").arg(name).arg(root.who))
                finish(ok, ok ? "" : code === "save_conflict_stale" ? qsTr("The save changed meanwhile. Nothing was uploaded; open it again.")
                                   : root.failed(qsTr("Could not upload"), code, message))
            })
        })
    }

    // ------------------------------------------------------------ file dialogs
    Loader {
        id: saveLoader
        active: false
        source: "AdminSaveDialog.qml"
        onLoaded: {
            item.player = root.player
            item.title = qsTr("Save the save file")
            item.suffix = "sav"
            item.chosen.connect(root.saveTo)
            item.prepare(saveLoader.name)
            item.open()
        }
        property string name: ""
        onStatusChanged: if (status === Loader.Error) root.say(qsTr("The file dialog is not available in this installation."), "error")
    }
    QtObject {
        id: saveDialog
        function open(name: string) {
            saveLoader.name = name
            if (saveLoader.item) { saveLoader.item.prepare(name); saveLoader.item.open() }
            else saveLoader.active = true
        }
    }
    Loader {
        id: uploadLoader
        active: false
        source: "ServerSystemsFileDialog.qml"
        onLoaded: {
            item.title = qsTr("Choose a save file")
            item.chosen.connect(root.stageUpload)
            item.open()
        }
        onStatusChanged: if (status === Loader.Error) root.say(qsTr("The file dialog is not available in this installation."), "error")
    }
    function pickUpload() {
        if (uploadLoader.item) uploadLoader.item.open()
        else uploadLoader.active = true
    }

    ColumnLayout {
        id: col
        width: root.width
        spacing: Theme.space2
        // ------------------------------------------------------------ view
        UzSectionLabel {
            Layout.fillWidth: true
            text: root.save ? qsTr("%1 · %2 · slot %3").arg(root.who).arg(root.save.game_title).arg(root.save.slot) : ""
            meta: root.current ? qsTr("Rev %1 · %2").arg(root.current.revision).arg(root.current.device_name) : ""
        }
        UzStateBlock {
            objectName: "savesAdminLoading"
            Layout.fillWidth: true
            visible: root.phase === "loading"
            kind: "loading"
            title: qsTr("Reading the save")
        }
        UzStateBlock {
            objectName: "savesAdminError"
            Layout.fillWidth: true
            visible: root.phase === "error"
            kind: "error"
            title: qsTr("Could not read this save")
            text: qsTr("It may have been removed. Try again.")
            action: qsTr("Try again")
            onActionClicked: root.load()
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.phase === "ready"
            spacing: Theme.space2

            UzStatusIcon {
                objectName: "savesAdminNote"
                Layout.fillWidth: true
                visible: root.note !== ""
                kind: root.noteKind
                small: true
                text: root.note
                maxWidth: 640
            }

            // ---- conflict (3s-app-1)
            ColumnLayout {
                objectName: "savesAdminConflict"
                Layout.fillWidth: true
                visible: root.conflict !== null
                spacing: Theme.space1
                UzStatusIcon {
                    Layout.fillWidth: true
                    kind: "warning"
                    text: qsTr("Two versions were made on two devices at nearly the same time. Pick the one to keep; the other stays in the history.")
                    maxWidth: 640
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: width >= 520 ? 2 : 1
                    columnSpacing: Theme.space2
                    rowSpacing: Theme.space2
                    Repeater {
                        model: root.conflict === null ? [] : [
                            { name: "Hub", title: qsTr("On the server · Rev %1").arg(root.conflict.hub.revision), device: root.conflict.hub.device_name, when: root.conflict.hub.created_at, side: "hub" },
                            { name: "Local", title: qsTr("Uploaded · v%1").arg(root.conflict.secured.version), device: root.conflict.secured.device_name, when: root.conflict.secured.created_at, side: "local" }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            implicitHeight: side.implicitHeight + Theme.space3
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.rule
                            ColumnLayout {
                                id: side
                                anchors.fill: parent
                                anchors.margins: Theme.space15
                                spacing: Theme.space05
                                UzMono { text: modelData.title.toUpperCase() }
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText; font.family: Theme.sans; font.pixelSize: Theme.fontSmall; color: Theme.ink; text: modelData.device }
                                UzMono { text: root.whenText(modelData.when) }
                                UzButton {
                                    objectName: "savesAdminKeep" + modelData.name
                                    Layout.topMargin: Theme.space05
                                    size: "sm"
                                    text: qsTr("Keep this")
                                    onClicked: root.keep(modelData.side)
                                }
                            }
                        }
                    }
                }
            }

            // ---- actions
            Flow {
                Layout.fillWidth: true
                spacing: Theme.space1
                UzButton { objectName: "savesAdminDownloadLatest"; size: "sm"; text: qsTr("Download latest"); onClicked: root.download(0) }
                UzButton { objectName: "savesAdminUpload"; size: "sm"; text: qsTr("Upload save file…"); onClicked: root.pickUpload() }
                UzButton { objectName: "savesAdminSnapshot"; size: "sm"; text: qsTr("Keep as snapshot…"); onClicked: root.snapshot() }
                UzButton { objectName: "savesAdminNewSlot"; size: "sm"; text: qsTr("New slot from this…"); onClicked: root.newSlot() }
                UzButton { objectName: "savesAdminRename"; size: "sm"; text: qsTr("Rename slot…"); enabled: !root.isDefault; onClicked: root.renameSlot() }
                UzButton { objectName: "savesAdminDeleteSlot"; size: "sm"; kind: "danger"; enabled: root.conflict === null; text: qsTr("Delete slot…"); onClicked: root.deleteSlot() }
            }
            UzMono {
                Layout.fillWidth: true
                visible: root.isDefault || root.conflict !== null
                wrapMode: Text.Wrap
                text: root.conflict !== null ? qsTr("Resolve the conflict before deleting the slot.") : qsTr("The main slot keeps its name.")
            }

            // ---- history
            UzSectionLabel { Layout.fillWidth: true; text: qsTr("History"); meta: Theme.plural(root.versions.length, qsTr("version"), qsTr("versions")) }
            UzTable {
                objectPrefix: "savesAdminHistory"
                Layout.fillWidth: true
                selectable: false
                emptyText: qsTr("No older versions yet.")
                columns: [
                    { key: "version", title: qsTr("Version"), width: 72, mono: true },
                    { key: "when", title: qsTr("When"), fill: 1.1 },
                    { key: "device", title: qsTr("Device"), fill: 1 },
                    { key: "kind", title: qsTr("What"), fill: 1.2 },
                    { key: "actions", title: "", width: 250, align: "right", custom: true }
                ]
                rows: root.versions.map(v => ({ version: "v" + v.version, when: root.whenText(v.created_at), device: v.device_name, kind: root.kindText(v), raw: v }))
                cellDelegate: Component {
                    Item {
                        id: cell
                        property var row: null
                        property var column: null
                        implicitHeight: Theme.row44
                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 0
                            UzButton { objectName: cell.row ? "savesAdminRestore_" + cell.row.raw.version : ""; kind: "link"; text: qsTr("Restore…"); onClicked: root.restore(cell.row.raw) }
                            UzButton { objectName: cell.row ? "savesAdminDownload_" + cell.row.raw.version : ""; kind: "link"; text: qsTr("Download"); onClicked: root.download(cell.row.raw.version) }
                            UzButton { objectName: cell.row ? "savesAdminDeleteVersion_" + cell.row.raw.version : ""; kind: "link"; text: qsTr("Delete…"); onClicked: root.deleteVersion(cell.row.raw) }
                        }
                    }
                }
            }
        }
    }
}
