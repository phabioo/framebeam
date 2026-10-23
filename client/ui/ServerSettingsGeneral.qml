import QtQuick
import QtQuick.Layouts
import Uzume.App

// Server › General (accounts-and-server.md 3q-app-1): Server name (Rename...), Import folder (Change...), Members can add games
// (toggle) and Run setup again (Open..., only where the app has the setup assistant). From GET /admin/settings and PATCH
// /admin/settings; the import folder is the network setting `import_dir`.
// Left out, the API carries nothing for it: "Joining" (Invite only is the only way to join).
// objectNames: settingsGeneral, generalNameRow, generalRename, generalImportRow, generalImportChange, generalUploadsRow, generalUploads,
// generalSetupRow, generalSetupOpen.
ColumnLayout {
    id: root
    objectName: "settingsGeneral"
    required property var pane
    readonly property var s: root.pane.settings
    readonly property var importDir: root.pane.netSetting("import_dir")
    readonly property bool setupAvailable: typeof root.pane.player.showSetupAssistant === "function"
    spacing: 0

    Component.onCompleted: root.pane.helpItems = [
        { title: qsTr("Who this affects"), text: qsTr("Settings here affect everyone on %1. Your own settings are under This PC.").arg(root.pane.player.hubName) },
        { title: qsTr("Invites"), text: qsTr("Invites are made in Users › Invites.") }
    ]

    UzSettingsRow {
        objectName: "generalNameRow"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        resetMode: "none"
        idKey: "name"
        label: qsTr("Server name")
        descriptionName: "generalName"
        description: root.s.hub_name ?? ""
        ctrl: "buttons"
        buttons: [{ name: "generalRename", text: qsTr("Rename…") }]
        onButtonClicked: root.pane.openEdit({
            title: qsTr("Rename the server"), text: qsTr("Apps show this name in their server list."), label: qsTr("Server name"),
            value: root.s.hub_name, action: qsTr("Rename"),
            save: function (v) { root.pane.patchSettings({ name: v }, qsTr("Renamed the server to %1.").arg(v)) }
        })
    }
    UzSettingsRow {
        objectName: "generalImportRow"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        resetMode: "none"
        idKey: "import"
        label: qsTr("Import folder")
        descriptionName: "generalImport"
        description: (root.importDir.value !== "" ? root.importDir.value + " · " : "") + qsTr("Files in it are added when you rescan the folders.")
        badge: root.importDir.restart_pending ? qsTr("applies after restart") : ""
        ctrl: "buttons"
        buttons: [
            { name: "generalImportChange", text: qsTr("Change…") },
            { name: "generalImportReset", text: qsTr("Use default"), visible: root.importDir.source === "web" }
        ]
        onButtonClicked: name => {
            if (name === "generalImportReset") {
                root.pane.resetNet("import_dir", qsTr("The import folder is back to the server's own value."))
                return
            }
            root.pane.openEdit({
                title: qsTr("Change the import folder"), text: qsTr("A folder on the server's computer. It applies after a restart."),
                label: qsTr("Folder"), value: root.importDir.value, mono: true, action: qsTr("Change"),
                save: function (v) { root.pane.saveNet("import_dir", { value: v }, qsTr("Saved the import folder.")) }
            })
        }
    }
    UzSettingsRow {
        objectName: "generalUploadsRow"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        resetMode: "none"
        idKey: "uploads"
        label: qsTr("Members can add games")
        description: qsTr("Members upload game files from their own PC. Off: only admins add games.")
        ctrl: "toggle"
        controlName: "generalUploads"
        checked: root.s.allow_user_uploads === true
        onToggled: value => root.pane.patchSettings({ allow_user_uploads: value },
                                                    value ? qsTr("Members can add games now.") : qsTr("Only admins can add games now."))
    }
    UzSettingsRow {
        objectName: "generalSetupRow"
        visible: root.setupAvailable
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        resetMode: "none"
        idKey: "setup"
        label: qsTr("Run setup again")
        description: qsTr("Opens the setup assistant. Nothing is reset.")
        ctrl: "buttons"
        buttons: [{ name: "generalSetupOpen", text: qsTr("Open…") }]
        onButtonClicked: root.pane.player.showSetupAssistant()
    }
}
