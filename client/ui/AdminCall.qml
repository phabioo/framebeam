import QtQuick
import Uzume.App

// Calls of the admin pieces that live inside other screens (Library, Saves, Runs, What's new, Setup assistant): sends a
// request through `player.admin` and hands the answer to the callback given with it.
//   call(method, path, body, done)                  done(ok, data, code, message); the 403 "Confirm it's you" round trip is
//                                                   handled by AdminController (parked, asked, replayed)
//   upload(method, path, file, type, headers, done) done(ok, data, code, message)
//   download(path, file, done)                      done(ok, filePath, message)
//   failText(what, message)                         "<what>: <message>" or "<what>: the server did not answer."
QtObject {
    id: root
    required property AdminController admin
    property var calls: ({})

    function remember(id: int, done: var) {
        const c = Object.assign({}, root.calls)
        c[id] = done
        root.calls = c
    }
    function take(id: int): var {
        const cb = root.calls[id]
        if (cb === undefined) return undefined
        const c = Object.assign({}, root.calls)
        delete c[id]
        root.calls = c
        return cb
    }
    function call(method: string, path: string, body: var, done: var): int {
        const id = root.admin.request(method, path, body)
        root.remember(id, done)
        return id
    }
    function upload(method: string, path: string, file: string, type: string, headers: var, done: var): int {
        const id = root.admin.upload(method, path, file, type, headers)
        root.remember(id, done)
        return id
    }
    function download(path: string, file: string, done: var): int {
        const id = root.admin.download(path, file)
        root.remember(id, done)
        return id
    }
    function failText(what: string, message: string): string {
        return message !== "" ? qsTr("%1: %2").arg(what).arg(message) : qsTr("%1: the server did not answer.").arg(what)
    }

    property Connections link: Connections {
        target: root.admin
        function onReplied(id, ok, status, data, code, message) {
            const cb = root.take(id)
            if (cb !== undefined) cb(ok, data, code, message)
        }
        function onDownloadFinished(id, ok, filePath, message) {
            const cb = root.take(id)
            if (cb !== undefined) cb(ok, filePath, message)
        }
    }
}
