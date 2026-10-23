import QtQuick
import QtQuick.Layouts
import Uzume.App

// Users (accounts-and-server.md 3n-app-1 to 3n-app-3, 3m-app-1, 3m-app-2, 8f-2): Tabs People · Invites · Devices on AdminPage.
// Data from GET /admin/users, /admin/devices, /admin/invites and /admin/pairing-requests, read when the page opens and again
// after every change. The tabs are ServerUsersPeople / ServerUsersInvites / ServerUsersDevices, the dialogs and the Toast are
// ServerUsersDialogs; this file holds the data, the actions and the states.
// What the API does not carry is left out: no play time, no "playing" status, no password state per person, no role or number of
// uses per invite, no "made by", no invite link (only the code), no network or "has no password" text on a waiting device,
// no warning about a save that is not uploaded.
Item {
    id: root
    objectName: "serverUsersPage"
    required property PlayerController player
    readonly property AdminController admin: root.player.admin

    property string tab: "People"
    property var users: []
    property var devices: []
    property var invites: []
    property var requests: []
    property string selectedId: ""
    property var approveChoice: ({})           // request id -> user id picked in "Approve as"
    property bool loaded: false
    property bool loadFailed: false
    property string loadError: ""
    property int loadsLeft: 0
    property bool loadHadError: false
    property string note: ""
    property string noteKind: "done"
    property var handlers: ({})
    property var inFlight: null

    // ------------------------------------------------------------ derived data
    readonly property string selfName: root.player.userName
    readonly property string thisDeviceId: root.admin.thisDeviceId()
    readonly property int adminCount: root.users.filter(u => u.role === "admin" && !u.disabled).length
    readonly property var selected: root.userById(root.selectedId)
    readonly property int selectedIndex: root.peopleRows.findIndex(r => r.id === root.selectedId)
    readonly property var peopleRows: root.users.map(u => ({
        id: u.id, person: u.display_name, since: qsTr("Since %1").arg(root.monthText(u.created_at)),
        role: u.role === "admin" ? qsTr("Admin") : qsTr("Member"), status: root.statusText(u), devices: String(u.devices ?? 0) }))
    readonly property var inviteRows: root.invites.map(i => ({
        id: i.id, kind: i.for_user ? qsTr("Pairing code for %1").arg(i.for_user) : qsTr("Invite"),
        trust: i.authorize_device ? qsTr("Trusted at once") : qsTr("Needs approval"),
        status: root.inviteStatus(i.status), expires: i.status === "active" ? root.dateText(i.expires_at) : "—",
        usedBy: i.redeemed_by ? i.redeemed_by : "—", open: i.status === "active" }))
    readonly property int openInvites: root.invites.filter(i => i.status === "active").length
    readonly property var deviceRows: root.devices.map(d => ({
        id: d.id, person: d.user_name, device: d.id === root.thisDeviceId ? qsTr("%1 · this PC").arg(d.name) : d.name,
        app: d.player_version, seen: d.status === "revoked" ? qsTr("Signed out") : d.online ? qsTr("Now") : root.agoText(d.last_seen_at),
        trusted: d.status === "trusted", name: d.name, userName: d.user_name }))
    readonly property var userOptions: root.users.filter(u => !u.disabled).map(u => ({ value: u.id, label: u.display_name }))

    function userById(id: string): var {
        for (const u of root.users) if (u.id === id) return u
        return null
    }
    function isSelf(u: var): bool { return u !== null && root.selfName !== "" && u.display_name === root.selfName }
    function devicesOf(u: var): var {
        return root.devices.filter(d => d.user_id === u.id && d.status === "trusted").map(d => d.name)
    }
    function isOnline(u: var): bool { return root.devices.some(d => d.user_id === u.id && d.status === "trusted" && d.online) }
    function statusText(u: var): string {
        if (u.disabled) return qsTr("Can't sign in")
        if (root.isOnline(u)) return qsTr("Online")
        return u.last_seen_at ? qsTr("Offline · %1").arg(root.agoText(u.last_seen_at)) : qsTr("Never signed in")
    }
    function inviteStatus(s: string): string {
        switch (s) {
        case "active": return qsTr("Open")
        case "redeemed": return qsTr("Used")
        case "revoked": return qsTr("Withdrawn")
        default: return qsTr("Expired")
        }
    }
    function monthText(iso: string): string {
        const d = new Date(iso)
        return isNaN(d.getTime()) ? "" : Qt.formatDate(d, "MMM yyyy")
    }
    function dateText(iso: string): string {
        const d = new Date(iso)
        return isNaN(d.getTime()) ? "—" : Qt.formatDate(d, "d MMM")
    }
    function timeText(iso: string): string {
        const d = new Date(iso)
        if (isNaN(d.getTime())) return ""
        const today = new Date()
        const same = d.getFullYear() === today.getFullYear() && d.getMonth() === today.getMonth() && d.getDate() === today.getDate()
        return same ? qsTr("today %1").arg(Qt.formatTime(d, "HH:mm")) : qsTr("%1 %2").arg(Qt.formatDate(d, "d MMM")).arg(Qt.formatTime(d, "HH:mm"))
    }
    function agoText(iso: string): string {
        const d = new Date(iso)
        if (!iso || isNaN(d.getTime())) return "—"
        const min = Math.max(0, Math.floor((Date.now() - d.getTime()) / 60000))
        if (min < 1) return qsTr("Just now")
        if (min < 60) return qsTr("%1 min").arg(min)
        if (min < 1440) return qsTr("%1 h").arg(Math.floor(min / 60))
        return Theme.plural(Math.floor(min / 1440), qsTr("day"), qsTr("days"))
    }
    function errorText(code: string, message: string): string {
        if (code === "confirmation_cancelled") return qsTr("Cancelled. Nothing has changed.")
        return message !== "" ? message : qsTr("The server did not answer.")
    }

    // ------------------------------------------------------------ calls
    function call(method: string, path: string, body: var, done: var) {
        // A reply can be emitted before request() returns: the handler in flight takes it then.
        root.inFlight = done
        const id = body === undefined ? root.admin.request(method, path) : root.admin.request(method, path, body)
        if (root.inFlight !== null) root.handlers[id] = done
        root.inFlight = null
    }
    Connections {
        target: root.admin
        function onReplied(id, ok, status, data, code, message) {
            const h = root.handlers[id] ?? root.inFlight
            if (!h) return
            if (root.handlers[id] === undefined) root.inFlight = null
            delete root.handlers[id]
            h(ok, data, code, message, status)
        }
    }
    function say(text: string, kind: string) {
        root.note = text
        root.noteKind = kind ?? "done"
    }

    property bool reloadAgain: false
    function reload() {
        if (root.loadsLeft > 0) { root.reloadAgain = true; return }
        root.loadsLeft = 4
        root.loadHadError = false
        const finish = () => {
            root.loadsLeft = Math.max(0, root.loadsLeft - 1)
            if (root.loadsLeft === 0) {
                if (root.reloadAgain) { root.reloadAgain = false; Qt.callLater(root.reload) }
                root.loaded = !root.loadHadError || root.loaded
                root.loadFailed = root.loadHadError && !root.loaded
                root.admin.refreshCounts()
            }
        }
        const read = (path, apply) => root.call("GET", path, undefined, (ok, data, code, message) => {
            if (ok && data) apply(data)
            else { root.loadHadError = true; root.loadError = root.errorText(code, message) }
            finish()
        })
        read("/admin/users", d => { root.users = d.users ?? [] })
        read("/admin/devices", d => { root.devices = d.devices ?? [] })
        read("/admin/invites", d => { root.invites = d.invites ?? [] })
        read("/admin/pairing-requests", d => { root.requests = d.requests ?? [] })
    }
    onVisibleChanged: if (visible) root.reload()
    Component.onCompleted: root.reload()

    // ------------------------------------------------------------ actions
    function createInvite(expiry: string, trustDevice: bool, finish: var) {
        root.call("POST", "/admin/invites", { expiry: expiry, authorize_device: trustDevice }, (ok, data, code, message) => {
            if (!ok) { finish(false, root.errorText(code, message)); return }
            finish(true, "")
            root.reload()
            root.tab = "Invites"
            const until = data && data.code ? root.dateText(data.code.expires_at) : ""
            dialogs.showCode({ label: qsTr("Users · Invites"), title: qsTr("Send this to your friend"),
                               text: qsTr("The code works until %1 and is shown only now.").arg(until),
                               code: data && data.code ? data.code.code : "", note: qsTr("Shown once"), kind: "invite" })
        })
    }
    function withdrawInvite(row: var) {
        dialogs.ask({ title: qsTr("Withdraw this invite?"), text: qsTr("The code stops working at once."), action: qsTr("Withdraw"), danger: false,
                      run: (finish) => root.call("DELETE", "/admin/invites/" + encodeURIComponent(row.id), undefined, (ok, d, code, message) => {
                          finish(ok, ok ? "" : root.errorText(code, message))
                          if (ok) { root.say(qsTr("Invite withdrawn."), "done"); root.reload() }
                      }) })
    }
    function approve(req: var) {
        const target = root.approveChoice[req.id] ?? req.user_id ?? (root.userOptions.length > 0 ? root.userOptions[0].value : "")
        if (target === "") { root.say(qsTr("There is nobody to approve this PC for."), "error"); return }
        const u = root.userById(target)
        root.call("POST", "/admin/pairing-requests/" + encodeURIComponent(req.id) + "/allow", { user_id: target }, (ok, d, code, message) => {
            if (ok) {
                root.say(qsTr("%1 is approved. %2 is signed in there now.").arg(req.device_name).arg(u ? u.display_name : ""), "done")
                dialogs.toast(qsTr("%1 is approved").arg(req.device_name), "", null)
            } else {
                root.say(root.errorText(code, message), "error")
            }
            root.reload()
        })
    }
    function deny(req: var) {
        dialogs.ask({ title: qsTr("Deny %1?").arg(req.device_name),
                      text: qsTr("The PC shows that the request was declined. It can ask again after 10 minutes."),
                      action: qsTr("Deny"), danger: true,
                      run: (finish) => root.call("POST", "/admin/pairing-requests/" + encodeURIComponent(req.id) + "/deny", undefined, (ok, d, code, message) => {
                          finish(ok, ok ? "" : root.errorText(code, message))
                          if (ok) root.say(qsTr("%1 was denied.").arg(req.device_name), "done")
                          root.reload()
                      }) })
    }
    function signOutDevice(row: var) {
        dialogs.ask({ title: qsTr("Sign out %1?").arg(row.name),
                      text: qsTr("%1 loses access at once. Saves stay. The PC can ask to be paired again.").arg(row.name),
                      action: qsTr("Sign out"), danger: true,
                      run: (finish) => root.call("POST", "/admin/devices/" + encodeURIComponent(row.id) + "/revoke", undefined, (ok, d, code, message) => {
                          finish(ok, ok ? "" : root.errorText(code, message))
                          if (ok) root.say(qsTr("%1 is signed out.").arg(row.name), "done")
                          root.reload()
                      }) })
    }
    function turnOff(u: var) {
        dialogs.ask({ title: qsTr("Turn off %1?").arg(u.display_name),
                      text: qsTr("%1 is signed out everywhere and can't sign in. Saves, play time and devices stay.").arg(u.display_name),
                      action: qsTr("Turn off"), danger: true,
                      run: (finish) => root.call("POST", "/admin/users/" + encodeURIComponent(u.id) + "/disable", undefined, (ok, d, code, message) => {
                          finish(ok, ok ? "" : root.errorText(code, message))
                          if (ok) {
                              dialogs.toast(qsTr("%1 is turned off. Saves, play time and devices stay.").arg(u.display_name), qsTr("Undo"), () => root.turnOn(u, true))
                              root.say(qsTr("Turned off · saves kept"), "done")
                          }
                          root.reload()
                      }) })
    }
    function turnOn(u: var, undo: bool) {
        root.call("POST", "/admin/users/" + encodeURIComponent(u.id) + "/enable", undefined, (ok, d, code, message) => {
            if (ok) dialogs.toast(qsTr("%1 is turned on.").arg(u.display_name), "", null)
            else root.say(root.errorText(code, message), "error")
            root.reload()
        })
    }
    function setRole(u: var, role: string, password: string, finish: var) {
        const body = role === "admin" ? { role: "admin", password: password } : { role: "user" }
        root.call("POST", "/admin/users/" + encodeURIComponent(u.id) + "/role", body, (ok, d, code, message) => {
            finish(ok, ok ? "" : root.errorText(code, message))
            if (ok) root.say(role === "admin" ? qsTr("%1 is an admin now.").arg(u.display_name) : qsTr("%1 is a member now.").arg(u.display_name), "done")
            root.reload()
        })
    }
    function openInviteDialog() { dialogs.openInvite() }
    function makeAdmin(u: var) { dialogs.openMakeAdmin(u) }
    function makeMember(u: var) {
        dialogs.ask({ title: qsTr("Make %1 a member?").arg(u.display_name),
                      text: qsTr("%1 can no longer change users, backups or the server.").arg(u.display_name),
                      action: qsTr("Make member"), danger: false,
                      run: (finish) => root.setRole(u, "user", "", finish) })
    }
    function resetPassword(u: var) {
        dialogs.ask({ title: qsTr("Reset the password of %1?").arg(u.display_name),
                      text: qsTr("%1 gets a temporary password and must choose a new one at the next sign-in. Devices stay signed in.").arg(u.display_name),
                      action: qsTr("Reset password"), danger: true,
                      run: (finish) => root.call("POST", "/admin/users/" + encodeURIComponent(u.id) + "/password-reset", undefined, (ok, data, code, message) => {
                          finish(ok, ok ? "" : root.errorText(code, message))
                          if (!ok) return
                          dialogs.showCode({ label: qsTr("Users · Password"), title: qsTr("Temporary password for %1").arg(u.display_name),
                                             text: qsTr("Give it to %1 in person. It is shown only now.").arg(u.display_name),
                                             code: data ? data.temporary_password : "", note: qsTr("Shown once · new one at next sign-in"), kind: "password" })
                      }) })
    }
    function newPairingCode(u: var) {
        dialogs.ask({ title: qsTr("New pairing code for %1?").arg(u.display_name),
                      text: qsTr("A one-time code that works for 24 hours. %1 enters it in the app to pair a PC.").arg(u.display_name),
                      action: qsTr("Create code"), danger: false,
                      run: (finish) => root.call("POST", "/admin/users/" + encodeURIComponent(u.id) + "/code", undefined, (ok, data, code, message) => {
                          finish(ok, ok ? "" : root.errorText(code, message))
                          if (!ok) return
                          dialogs.showCode({ label: qsTr("Users · Pairing"), title: qsTr("Pairing code for %1").arg(u.display_name),
                                             text: qsTr("The code works until %1 and is shown only now.").arg(data && data.expires_at ? root.timeText(data.expires_at) : ""),
                                             code: data ? data.code : "", note: qsTr("Shown once"), kind: "pairing" })
                          root.reload()
                      }) })
    }
    // Parity with the old web admin: rename a person, add a person directly (with a pairing code), rename a device.
    function renameUser(u: var) {
        dialogs.askName({ title: qsTr("Rename %1").arg(u.display_name), text: qsTr("The name others see. The sign-in name stays."),
                          label: qsTr("Name"), value: u.display_name, action: qsTr("Rename"), max: 32,
                          run: (value, finish) => root.call("PATCH", "/admin/users/" + encodeURIComponent(u.id), { display_name: value }, (ok, d, code, message) => {
                              finish(ok, ok ? "" : root.errorText(code, message))
                              if (ok) root.say(qsTr("%1 is now called %2.").arg(u.display_name).arg(value), "done")
                              root.reload()
                          }) })
    }
    function addPerson() {
        dialogs.askName({ title: qsTr("Add a person"), text: qsTr("Creates the person and a one-time pairing code. The first PC that enters the code is paired with them."),
                          label: qsTr("Name"), value: "", action: qsTr("Add person"), max: 32,
                          run: (value, finish) => root.call("POST", "/admin/users", { display_name: value }, (ok, data, code, message) => {
                              finish(ok, ok ? "" : root.errorText(code, message))
                              if (!ok) return
                              const pc = data && data.pairing_code ? data.pairing_code : {}
                              dialogs.showCode({ label: qsTr("Users · Pairing"), title: qsTr("Pairing code for %1").arg(value),
                                                 text: qsTr("The code works until %1 and is shown only now.").arg(pc.expires_at ? root.timeText(pc.expires_at) : ""),
                                                 code: pc.code ?? "", note: qsTr("Shown once"), kind: "pairing" })
                              root.say(qsTr("%1 was added.").arg(value), "done")
                              root.reload()
                          }) })
    }
    function renameDevice(row: var) {
        dialogs.askName({ title: qsTr("Rename %1").arg(row.name), text: qsTr("The name shown in the list of devices and in the save history."),
                          label: qsTr("Name"), value: row.name, action: qsTr("Rename"), max: 100,
                          run: (value, finish) => root.call("PATCH", "/admin/devices/" + encodeURIComponent(row.id), { name: value }, (ok, d, code, message) => {
                              finish(ok, ok ? "" : root.errorText(code, message))
                              if (ok) root.say(qsTr("The device is now called %1.").arg(value), "done")
                              root.reload()
                          }) })
    }
    function removeUser(u: var) {
        dialogs.ask({ title: qsTr("Remove %1?").arg(u.display_name),
                      text: qsTr("%1 is deleted for good, with their devices, saves and save history. Games they added to the library stay. To keep the saves, turn %1 off instead.").arg(u.display_name),
                      action: qsTr("Remove"), danger: true,
                      run: (finish) => root.call("DELETE", "/admin/users/" + encodeURIComponent(u.id), undefined, (ok, d, code, message) => {
                          finish(ok, ok ? "" : root.errorText(code, message))
                          if (ok) { root.selectedId = ""; root.say(qsTr("%1 was removed.").arg(u.display_name), "done") }
                          root.reload()
                      }) })
    }

    // ------------------------------------------------------------ page
    AdminPage {
        anchors.fill: parent
        player: root.player
        title: qsTr("Users")
        label: root.loaded ? qsTr("%1 · %2").arg(Theme.plural(root.users.length, qsTr("person"), qsTr("people")))
                                .arg(Theme.plural(root.devices.filter(d => d.status === "trusted").length, qsTr("device"), qsTr("devices")))
                           : qsTr("Server")
        area: qsTr("Users")
        helpItems: [
            { title: qsTr("People"), text: qsTr("Everyone who can use this server. Admins manage users, backups and the server; members play.") },
            { title: qsTr("Invites"), text: qsTr("A code lets a friend join. It is shown once, works until it expires and can be withdrawn.") },
            { title: qsTr("Devices"), text: qsTr("A new PC waits here until you approve it. Signing a device out ends its access; saves stay.") }
        ]

        headerActions: [
            UzButton {
                objectName: "usersAddPerson"
                text: qsTr("Add person…")
                enabled: root.loaded
                onClicked: root.addPerson()
            },
            UzButton {
                objectName: "usersCreateInvite"
                kind: "primary"
                text: qsTr("Create invite…")
                enabled: root.loaded
                onClicked: dialogs.openInvite()
            }
        ]

        notice: [
            UzStatusIcon {
                objectName: "usersNote"
                visible: root.note !== ""
                kind: root.noteKind
                small: true
                text: root.note
                maxWidth: 640
            }
        ]

        UzTabs {
            objectName: "usersTabs"
            items: ["People", "Invites", "Devices"]
            active: root.tab
            counts: ({ "Devices": root.requests.length })
            onPicked: (label) => root.tab = label
        }

        UzStateBlock {
            objectName: "usersLoading"
            Layout.fillWidth: true
            visible: !root.loaded && !root.loadFailed
            kind: "loading"
            title: qsTr("Loading users")
        }
        UzStateBlock {
            objectName: "usersError"
            Layout.fillWidth: true
            visible: root.loadFailed
            kind: "error"
            title: qsTr("Can't read the users")
            text: root.loadError
            action: qsTr("Try again")
            onActionClicked: { root.loadFailed = false; root.reload() }
        }

        ServerUsersPeople { Layout.fillWidth: true; page: root; visible: root.loaded && root.tab === "People" }
        ServerUsersInvites { Layout.fillWidth: true; page: root; visible: root.loaded && root.tab === "Invites" }
        ServerUsersDevices { Layout.fillWidth: true; page: root; visible: root.loaded && root.tab === "Devices" }
    }

    ServerUsersDialogs {
        id: dialogs
        anchors.fill: parent
        page: root
    }
}
