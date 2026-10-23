import QtQuick
import QtQuick.Layouts
import Uzume.App

// Library, admin part 1 (accounts-and-server.md 3j-app-1 and 3j-app-2): "Rescan folders" with its progress and result line.
// POST /admin/library/rescan starts the scan of the server's import folder in the background; GET /admin/library/rescan is
// followed once a second while it runs. People keep playing. The result line comes from the numbers of the API: new games,
// files already in the library (same SHA-256) and files that were not added (unsupported or failed, with the reason). The
// Library list is read again when a scan finished. Shown for admins only (`player.admin.available`, set by the caller).
// objectNames: libraryAdminBar, libraryRescan, libraryRescanProgress, libraryRescanResult, libraryRescanSkipped_<n>.
ColumnLayout {
    id: root
    objectName: "libraryAdminBar"
    required property PlayerController player
    readonly property AdminController admin: root.player.admin
    property bool running: false
    property bool starting: false
    property real done: 0
    property real total: 0
    property string note: ""
    property string noteKind: "done"
    property var skipped: []
    spacing: Theme.space1

    AdminCall { id: api; admin: root.admin }

    function summaryText(sum: var): string {
        const added = sum.added ?? 0
        const already = sum.already_here ?? 0
        const skippedCount = (sum.unsupported ?? 0) + (sum.failed ? sum.failed.length : 0)
        return qsTr("%1, %2, %3").arg(Theme.plural(added, qsTr("new game"), qsTr("new games")))
                                 .arg(qsTr("%1 already here").arg(already))
                                 .arg(qsTr("%1 not added").arg(skippedCount))
    }
    function adopt(scan: var) {
        if (!scan || scan.running === undefined) return
        root.running = scan.running === true
        root.done = scan.done ?? 0
        root.total = scan.total ?? 0
        if (root.running) { poll.start(); return }
        poll.stop()
        if (scan.last_error) {
            root.noteKind = "error"
            root.note = qsTr("The scan stopped: %1").arg(scan.last_error)
        } else if (scan.summary) {
            root.noteKind = "done"
            root.note = root.summaryText(scan.summary)
            const out = []
            for (const f of (scan.summary.failed ?? [])) out.push(f.file + " · " + f.reason)
            if ((scan.summary.unsupported ?? 0) > 0) out.push(qsTr("%1 not a game file (.gb, .gbc, .gba, .nds, .3ds only)").arg(scan.summary.unsupported))
            root.skipped = out.slice(0, 5)
            root.player.reloadLibrary()
        }
    }
    function start() {
        if (root.starting || root.running) return
        root.starting = true
        root.note = ""
        root.skipped = []
        api.call("POST", "/admin/library/rescan", undefined, function (ok, data, code, message) {
            root.starting = false
            if (ok || code === "conflict") {   // 409: a scan runs already, follow it
                if (data && data.running !== undefined) root.adopt(data)
                else { root.running = true; poll.start() }
                return
            }
            root.noteKind = "error"
            root.note = api.failText(qsTr("Could not start the scan"), message)
        })
    }
    // `quiet`: when the page opens only a scan that is running is picked up (the result of an old scan is not shown again).
    function refresh(quiet: bool) {
        api.call("GET", "/admin/library/rescan", undefined, function (ok, data) {
            if (ok && (!quiet || (data && data.running === true))) root.adopt(data)
        })
    }
    Timer { id: poll; interval: 800; repeat: true; onTriggered: root.refresh(false) }
    Component.onCompleted: if (root.visible && root.admin.available) root.refresh(true)
    onVisibleChanged: if (visible && root.admin.available && !root.running) root.refresh(true)

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space2
        UzButton {
            objectName: "libraryRescan"
            size: "sm"
            text: qsTr("Rescan folders")
            busy: root.starting || root.running
            enabled: !root.starting && !root.running
            onClicked: root.start()
        }
        UzStatusIcon {
            objectName: "libraryRescanResult"
            Layout.fillWidth: true
            visible: root.note !== "" && !root.running
            kind: root.noteKind
            small: true
            text: root.note
            maxWidth: 640
        }
        UzMono {
            objectName: "libraryRescanHint"
            Layout.fillWidth: true
            visible: root.note === "" && !root.running
            elide: Text.ElideRight
            text: qsTr("Reads the import folder of the server")
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.running
        spacing: Theme.space05
        UzProgress {
            objectName: "libraryRescanProgress"
            Layout.fillWidth: true
            Layout.maximumWidth: 480
            thin: true
            value: root.total > 0 ? root.done / root.total : 0
            hint: root.total > 0 ? qsTr("%1 of %2 files").arg(root.done).arg(root.total) : qsTr("Looking for files")
        }
        UzMono { text: qsTr("People keep playing while it runs.") }
    }
    Repeater {
        model: root.skipped
        delegate: UzMono {
            required property string modelData
            required property int index
            objectName: "libraryRescanSkipped_" + index
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            text: modelData
        }
    }
}
