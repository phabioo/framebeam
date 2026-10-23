import QtQuick
import QtQuick.Layouts
import Uzume.App

// Server › Diagnostics (accounts-and-server.md 3q-app-7): Metrics endpoint (toggle; GET / PUT /admin/metrics; the token is returned
// once when it is created, POST /admin/metrics/token makes a new one and the old one stops working), Connection check (Run...) and
// Collect server logs (POST /admin/diagnostics/bundle saved to a zip file through the save dialog).
// Left out, the API carries nothing for it: Log detail (Normal / Detailed).
// objectNames: settingsDiagnostics, metricsRow, metricsToggle, metricsAddress, metricsToken, metricsTokenCopy, metricsTokenDone, metricsNew,
// metricsNewDialog, metricsNewConfirm, checkRow, checkRun, bundleRow, bundleCollect, bundleNote, diagnosticsIncluded.
ColumnLayout {
    id: root
    objectName: "settingsDiagnostics"
    required property var pane
    property var metrics: null                // AdminMetrics
    property string token: ""                 // shown once, never stored
    property int bundleId: -1
    property bool bundleBusy: false
    property string bundleText: ""
    property string bundleKind: "done"
    readonly property string address: (root.pane.admin.status.public_address ?? "") !== "" ? root.pane.admin.status.public_address + "/metrics" : "/metrics"
    spacing: Theme.space4

    Component.onCompleted: {
        root.pane.helpItems = [
            { title: qsTr("Metrics endpoint"), text: qsTr("For Prometheus and similar tools. It is off by default and always needs the token.") },
            { title: qsTr("Collect server logs"), text: qsTr("Makes one zip file with the logs of the last days and the settings, to hand over when something does not work. Secrets are removed.") }
        ]
        root.pane.call("GET", "/admin/metrics", undefined, function (ok, data) { if (ok && data && data.enabled !== undefined) root.metrics = data })
        root.pane.admin.refreshStatus()
    }
    function setMetrics(on: bool) {
        root.pane.call("PUT", "/admin/metrics", { enabled: on }, function (ok, data, code, message) {
            if (ok && data && data.enabled !== undefined) {
                root.metrics = data
                root.token = data.token ?? ""
                root.pane.say(on ? qsTr("The metrics endpoint is on.") : qsTr("The metrics endpoint is off."), "done")
                root.pane.admin.refreshStatus()
            } else {
                root.pane.say(root.pane.failText(qsTr("Could not save"), message), "error")
                root.metrics = Object.assign({}, root.metrics)
            }
        })
    }
    function newToken() {
        root.pane.setBusy("token", true)
        root.pane.call("POST", "/admin/metrics/token", undefined, function (ok, data, code, message) {
            root.pane.setBusy("token", false)
            if (ok && data && data.token) {
                root.token = data.token
                const m = Object.assign({}, root.metrics)
                m.token_set = true
                root.metrics = m
                root.pane.say(qsTr("Made a new token. The old one stopped working."), "done")
            } else {
                root.pane.say(root.pane.failText(qsTr("Could not make a token"), message), "error")
            }
        })
    }
    function suggestedName(): string {
        const name = root.pane.settings.hub_name ?? "server"
        return "uzume-diag-" + name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") + ".zip"
    }
    function saveBundle(pathOrUrl: string) {
        root.bundleText = ""
        root.bundleBusy = true
        root.bundleId = root.pane.admin.download("/admin/diagnostics/bundle", pathOrUrl, "POST")
    }
    Connections {
        target: root.pane.admin
        function onDownloadFinished(id, ok, filePath, message) {
            if (id !== root.bundleId) return
            root.bundleBusy = false
            root.bundleKind = ok ? "done" : "error"
            root.bundleText = ok ? qsTr("Saved the server logs to %1").arg(filePath)
                                 : qsTr("Could not collect the server logs: %1").arg(message !== "" ? message : qsTr("the server did not answer."))
        }
    }
    Loader {
        id: saveDialog
        active: false
        source: "AdminSaveDialog.qml"
        function open() {
            if (item) {
                item.prepare(root.suggestedName())
                item.open()
            } else {
                active = true
            }
        }
        onLoaded: {
            item.player = root.pane.player
            item.title = qsTr("Save server logs")
            item.suffix = "zip"
            item.chosen.connect(root.saveBundle)
            item.prepare(root.suggestedName())
            item.open()
        }
        onStatusChanged: if (status === Loader.Error) {
            root.bundleKind = "error"
            root.bundleText = qsTr("The file dialog is not available in this installation.")
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0
        UzSettingsRow {
            objectName: "metricsRow"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            resetMode: "none"
            idKey: "metrics"
            label: qsTr("Metrics endpoint")
            descriptionName: "metricsAddress"
            description: root.address + " · " + qsTr("needs the token")
            ctrl: "toggle"
            controlName: "metricsToggle"
            controlEnabled: root.metrics !== null
            checked: root.metrics !== null && root.metrics.enabled === true
            onToggled: value => root.setMetrics(value)
        }
        ColumnLayout {
            visible: root.metrics !== null && root.metrics.enabled === true
            Layout.fillWidth: true
            Layout.topMargin: Theme.space2
            Layout.bottomMargin: Theme.space2
            Layout.leftMargin: Theme.settingsDotSlot
            spacing: Theme.space1
            UzStatusIcon {
                objectName: "metricsTokenDone"
                visible: root.token !== ""
                kind: "warning"
                small: true
                maxWidth: 640
                text: qsTr("Copy the token now. It is shown only once; the server keeps just a fingerprint of it.")
            }
            RowLayout {
                visible: root.token !== ""
                spacing: Theme.space1
                UzMono { objectName: "metricsToken"; text: root.token; color: Theme.ink; textFormat: Text.PlainText }
                UzButton { objectName: "metricsTokenCopy"; size: "sm"; text: qsTr("Copy"); onClicked: root.pane.copyText(root.token) }
            }
            UzMono {
                visible: root.token === "" && root.metrics !== null && root.metrics.token_set === true
                text: qsTr("A token is set. It can't be shown again.")
            }
            UzButton {
                objectName: "metricsNew"
                size: "sm"
                busy: root.pane.isBusy("token")
                text: root.metrics !== null && root.metrics.token_set === true ? qsTr("Make a new token…") : qsTr("Make a token")
                onClicked: root.metrics !== null && root.metrics.token_set === true ? newDialog.open() : root.newToken()
            }
        }
        UzSettingsRow {
            objectName: "checkRow"
            visible: root.pane.player.connectionCheck.available
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            resetMode: "none"
            idKey: "check"
            label: qsTr("Connection check")
            description: qsTr("Tests the way from this PC to the server: server, STUN, relay and the kind of network. The server can't test itself from outside.")
            ctrl: "buttons"
            buttons: [{ name: "checkRun", text: qsTr("Run…") }]
            onButtonClicked: root.pane.player.connectionCheck.show()
        }
        UzSettingsRow {
            objectName: "bundleRow"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            resetMode: "none"
            idKey: "bundle"
            label: qsTr("Collect server logs")
            description: qsTr("The logs of the last days as one zip file: %1").arg(root.suggestedName())
            ctrl: "buttons"
            buttons: [{ name: "bundleCollect", text: qsTr("Collect…"), busy: root.bundleBusy }]
            onButtonClicked: saveDialog.open()
        }
    }
    UzStatusIcon {
        objectName: "bundleNote"
        visible: root.bundleText !== ""
        kind: root.bundleKind
        small: true
        text: root.bundleText
        maxWidth: 640
    }
    ColumnLayout {
        objectName: "diagnosticsIncluded"
        Layout.fillWidth: true
        Layout.maximumWidth: 640
        spacing: Theme.space05
        UzSectionLabel { Layout.fillWidth: true; text: qsTr("Included") }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            font.pixelSize: Theme.roles.small.size
            color: Theme.muted
            text: qsTr("Server log, settings without secrets, versions, disk and network summary.")
        }
        UzSectionLabel { Layout.fillWidth: true; Layout.topMargin: Theme.space1; text: qsTr("Never included") }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            font.pixelSize: Theme.roles.small.size
            color: Theme.muted
            text: qsTr("Passwords, invite codes, saves, chat, game files.")
        }
    }

    UzDialog {
        id: newDialog
        objectName: "metricsNewDialog"
        parent: root.pane.overlayParent
        label: qsTr("Diagnostics")
        title: qsTr("Make a new token?")
        text: qsTr("The old token stops working at once. Tools that use it need the new one.")
        actions: [UzButton { text: qsTr("Cancel"); onClicked: newDialog.close() }]
        dangerActions: [
            UzButton {
                objectName: "metricsNewConfirm"
                kind: "danger"
                text: qsTr("Replace")
                onClicked: { newDialog.close(); root.newToken() }
            }
        ]
    }
}
