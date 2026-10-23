import QtQuick
import Uzume.App

// The people of the server (GET /admin/users) as a lookup for admin pieces that show a user id: nameOf(id, fallback) is the
// display name, `known(id)` is false for a person who was removed. `loaded` turns true with the first answer.
QtObject {
    id: root
    required property AdminController admin
    property var byId: ({})
    property bool loaded: false

    function nameOf(id: string, fallback: string): string {
        const u = root.byId[id]
        if (u !== undefined) return u.display_name !== "" ? u.display_name : u.username
        return fallback !== "" ? fallback : id
    }
    function known(id: string): bool { return root.byId[id] !== undefined }
    function load() {
        api.call("GET", "/admin/users", undefined, function (ok, data) {
            if (!ok || !data || data.users === undefined) return
            const m = {}
            for (const u of data.users) m[u.id] = u
            root.byId = m
            root.loaded = true
        })
    }
    property AdminCall api: AdminCall { id: api; admin: root.admin }
}
