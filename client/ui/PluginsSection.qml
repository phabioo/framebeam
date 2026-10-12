import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Settings > Plugins (docs/design/plugins.md 4a, 4d): Developer mode, the installed plugins with their origin pill
// (Official / Hub upload · unverified / Local file), "Browse plugins on your Hub" (Install / Update), install from a
// .fbplugin file or a folder and, in Developer mode, link a folder. Turning on an unverified plugin asks first.
// Detail page and the full permission dialog (4b, 4c) come later.
ColumnLayout {
    id: root
    required property PlayerController player
    readonly property PluginsController ctl: player.plugins
    readonly property var list: ctl.plugins
    property bool pathPromptOpen: false
    property string pathMode: "file"   // file | install (folder) | link
    property string confirmUninstallId: ""
    property string confirmEnableId: ""
    property bool browseOpen: false
    spacing: Theme.space12

    Loader {
        id: dialogLoader
        active: false
        source: "PluginFolderDialog.qml"
        onLoaded: item.plugins = root.ctl
    }
    Loader {
        id: fileDialogLoader
        active: false
        source: "PluginFileDialog.qml"
        onLoaded: item.plugins = root.ctl
    }
    function openFolderDialog(mode: string) {
        root.pathMode = mode
        dialogLoader.active = true
        if (dialogLoader.status === Loader.Ready && dialogLoader.item !== null) {
            dialogLoader.item.mode = mode
            dialogLoader.item.open()
        } else {
            root.pathPromptOpen = true  // QtQuick.Dialogs not installed: type the path
        }
    }
    function openFileDialog() {
        root.pathMode = "file"
        fileDialogLoader.active = true
        if (fileDialogLoader.status === Loader.Ready && fileDialogLoader.item !== null) {
            fileDialogLoader.item.open()
        } else {
            root.pathPromptOpen = true
        }
    }

    Eyebrow { text: qsTr("Plugins") }
    FbLabel {
        Layout.fillWidth: true
        text: qsTr("If one fails it is paused; your game keeps running.")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
        wrapMode: Text.WordWrap
    }

    SettingsRow {
        objectName: "pluginDeveloperModeRow"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        resetMode: "none"
        label: qsTr("Developer mode")
        description: qsTr("Allows unsigned local plugins, adds the Plugin console tab in game and reloads plugins when their files change.")
        descriptionName: "pluginDeveloperModeHint"
        ctrl: "toggle"
        toggleName: "pluginDeveloperModeToggle"
        checked: root.ctl.developerMode
        onToggled: on => root.ctl.developerMode = on
    }

    Item {
        visible: root.list.length > 0
        Layout.fillWidth: true
        implicitHeight: eyebrowRow.implicitHeight
        RowLayout {
            id: eyebrowRow
            anchors.fill: parent
            Eyebrow { objectName: "pluginsInstalledTitle"; Layout.fillWidth: true; text: qsTr("Installed · %1").arg(root.list.length) }
        }
    }

    // ---- list
    Rectangle {
        objectName: "pluginsList"
        visible: root.list.length > 0
        Layout.fillWidth: true
        implicitHeight: listCol.implicitHeight
        radius: Theme.radius10
        color: Theme.surface
        border.width: 1
        border.color: Theme.borderCard
        ColumnLayout {
            id: listCol
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 0
            Repeater {
                model: root.list
                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    objectName: "pluginRow_" + modelData.id
                    Layout.fillWidth: true
                    implicitHeight: rowCol.implicitHeight + 28
                    Rectangle {
                        visible: row.index > 0
                        anchors.top: parent.top
                        width: parent.width
                        height: 1
                        color: Theme.borderRow
                    }
                    ColumnLayout {
                        id: rowCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            FbLabel {
                                objectName: "pluginName"
                                text: row.modelData.name
                                font.pixelSize: Theme.fontSection
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                Layout.maximumWidth: 260
                            }
                            FbMono {
                                objectName: "pluginVersion"
                                text: row.modelData.version
                                color: Theme.textMuted
                            }
                            PluginOriginPill {
                                objectName: "pluginOrigin"
                                origin: row.modelData.origin
                                text: row.modelData.originText
                            }
                            FbPill {
                                objectName: "pluginUpdateChip"
                                visible: row.modelData.updateVersion !== ""
                                small: true
                                tone: "neutral"
                                text: qsTr("Update available · %1").arg(row.modelData.updateVersion)
                            }
                            FbPill {
                                visible: row.modelData.devLinked
                                small: true
                                tone: "neutral"
                                text: qsTr("Linked folder")
                            }
                            Item { Layout.fillWidth: true }
                            FbButton {
                                objectName: "pluginUpdate"
                                visible: row.modelData.updateVersion !== ""
                                implicitHeight: 32
                                text: qsTr("Update")
                                onClicked: root.ctl.installHubPlugin(row.modelData.id, row.modelData.updateVersion)
                            }
                            FbToggle {
                                id: toggle
                                objectName: "pluginToggle"
                                enabled: row.modelData.canToggle
                                opacity: enabled ? 1 : 0.4
                                checked: row.modelData.enabled
                                onToggled: {
                                    if (checked && row.modelData.needsConfirm) {
                                        // Unverified: ask first (the toggle stays off until the user confirms).
                                        root.confirmEnableId = row.modelData.id
                                        checked = Qt.binding(function() { return row.modelData.enabled })
                                    } else {
                                        root.ctl.setPluginEnabled(row.modelData.id, checked)
                                    }
                                }
                            }
                            FbButton {
                                objectName: "pluginUninstall"
                                kind: "link"
                                text: qsTr("Uninstall")
                                onClicked: root.confirmUninstallId = row.modelData.id
                            }
                        }
                        FbLabel {
                            objectName: "pluginProblems"
                            visible: row.modelData.problemsText !== ""
                            Layout.fillWidth: true
                            text: "! " + row.modelData.problemsText
                            color: Theme.pluginWarn
                            font.pixelSize: Theme.fontMeta
                            wrapMode: Text.WordWrap
                        }
                        Rectangle {
                            objectName: "pluginEnableConfirm"
                            visible: root.confirmEnableId === row.modelData.id
                            Layout.fillWidth: true
                            implicitHeight: warnCol.implicitHeight + 24
                            radius: Theme.radius10
                            color: Theme.pluginWarnBg
                            border.width: 1
                            border.color: Theme.pluginWarnLine
                            ColumnLayout {
                                id: warnCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 14
                                anchors.rightMargin: 14
                                spacing: 8
                                FbLabel {
                                    objectName: "pluginUnverifiedWarning"
                                    Layout.fillWidth: true
                                    text: qsTr("FrameBeam has not checked this plugin. Turn it on only if you trust the admin of your Hub and where the file came from.")
                                    color: Theme.pluginWarnText
                                    font.pixelSize: Theme.fontMeta
                                    wrapMode: Text.WordWrap
                                }
                                FbLabel {
                                    objectName: "pluginEnablePermissions"
                                    Layout.fillWidth: true
                                    text: qsTr("It will be able to: %1").arg(row.modelData.permissionsText)
                                    color: Theme.pluginWarnText
                                    font.pixelSize: Theme.fontMeta
                                    wrapMode: Text.WordWrap
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Item { Layout.fillWidth: true }
                                    FbButton {
                                        objectName: "pluginEnableCancel"
                                        implicitHeight: 32
                                        text: qsTr("Cancel")
                                        onClicked: root.confirmEnableId = ""
                                    }
                                    FbButton {
                                        objectName: "pluginEnableConfirmButton"
                                        implicitHeight: 32
                                        kind: "primary"
                                        text: qsTr("Turn on")
                                        onClicked: {
                                            const id = row.modelData.id
                                            root.confirmEnableId = ""
                                            root.ctl.setPluginEnabled(id, true)
                                        }
                                    }
                                }
                            }
                        }
                        RowLayout {
                            visible: root.confirmUninstallId === row.modelData.id
                            Layout.fillWidth: true
                            spacing: 10
                            FbLabel {
                                Layout.fillWidth: true
                                text: row.modelData.devLinked ? qsTr("Remove the link? The folder itself is kept.") : qsTr("Remove this plugin from this device?")
                                font.pixelSize: Theme.fontMeta
                                color: Theme.textSecondary
                                wrapMode: Text.WordWrap
                            }
                            FbButton {
                                objectName: "pluginUninstallCancel"
                                implicitHeight: 32
                                text: qsTr("Cancel")
                                onClicked: root.confirmUninstallId = ""
                            }
                            FbButton {
                                objectName: "pluginUninstallConfirm"
                                implicitHeight: 32
                                kind: "primary"
                                text: qsTr("Uninstall")
                                onClicked: {
                                    const id = row.modelData.id
                                    root.confirmUninstallId = ""
                                    root.ctl.uninstall(id)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- empty state
    Rectangle {
        objectName: "pluginsEmpty"
        visible: root.list.length === 0
        Layout.fillWidth: true
        implicitHeight: emptyCol.implicitHeight + 48
        radius: Theme.radius10
        color: Theme.surface
        border.width: 1
        border.color: Theme.borderCard
        ColumnLayout {
            id: emptyCol
            anchors.centerIn: parent
            width: Math.min(440, parent.width - 48)
            spacing: 8
            Grid {
                Layout.alignment: Qt.AlignHCenter
                columns: 2
                spacing: 6
                Repeater {
                    model: 4
                    delegate: Rectangle {
                        required property int index
                        width: 24; height: 24; radius: 5
                        color: index === 0 ? Theme.accent : "transparent"
                        border.width: index === 0 ? 0 : 1.5
                        border.color: Theme.borderButton
                    }
                }
            }
            FbLabel { Layout.alignment: Qt.AlignHCenter; text: qsTr("No plugins yet"); font.pixelSize: Theme.fontSection; font.weight: Font.DemiBold }
            FbLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: root.ctl.hubSupported
                      ? qsTr("Plugins add panels and tools to your games. Browse what your Hub offers, or install a .fbplugin file or a plugin folder from this PC.")
                      : qsTr("Plugins add panels and tools to your games. Install a .fbplugin file or a plugin folder from this PC.")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
            }
        }
    }

    // ---- actions
    Flow {
        Layout.fillWidth: true
        spacing: Theme.space10
        FbButton {
            objectName: "pluginBrowseButton"
            visible: root.ctl.hubSupported
            implicitHeight: 36
            text: qsTr("Browse plugins on your Hub")
            onClicked: {
                root.browseOpen = !root.browseOpen
                if (root.browseOpen) root.ctl.refreshHubPlugins()
            }
        }
        FbButton {
            objectName: "pluginInstallButton"
            implicitHeight: 36
            text: qsTr("Install from file…")
            onClicked: root.openFileDialog()
        }
        FbButton {
            objectName: "pluginInstallFolderButton"
            implicitHeight: 36
            text: qsTr("Install from folder…")
            onClicked: root.openFolderDialog("install")
        }
        FbButton {
            objectName: "pluginLinkButton"
            visible: root.ctl.developerMode
            implicitHeight: 36
            text: qsTr("Link folder for development…")
            onClicked: root.openFolderDialog("link")
        }
    }
    // ---- browse plugins on the Hub
    Rectangle {
        objectName: "hubPluginsPanel"
        visible: root.browseOpen && root.ctl.hubSupported
        Layout.fillWidth: true
        implicitHeight: hubCol.implicitHeight
        radius: Theme.radius10
        color: Theme.surface
        border.width: 1
        border.color: Theme.borderCard
        ColumnLayout {
            id: hubCol
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 10
                Eyebrow { objectName: "hubPluginsTitle"; Layout.fillWidth: true; text: qsTr("On your Hub · %1").arg(root.ctl.hubPlugins.length) }
                FbLabel {
                    objectName: "hubPluginsStatus"
                    visible: root.ctl.hubStatus === "loading"
                    text: qsTr("Loading…")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontMeta
                }
                FbButton {
                    objectName: "hubPluginsRefresh"
                    kind: "link"
                    text: qsTr("Refresh")
                    onClicked: root.ctl.refreshHubPlugins()
                }
            }
            FbLabel {
                objectName: "hubPluginsError"
                visible: root.ctl.hubStatus === "failed"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 12
                text: qsTr("Could not load the list from your Hub: %1").arg(root.ctl.hubError)
                color: Theme.error
                font.pixelSize: Theme.fontMeta
                wrapMode: Text.WordWrap
            }
            FbLabel {
                objectName: "hubPluginsEmpty"
                visible: root.ctl.hubPlugins.length === 0 && root.ctl.hubStatus === "ready"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16
                text: qsTr("Your Hub offers no plugins yet. Its admin chooses what is offered.")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: root.ctl.hubPlugins
                delegate: Item {
                    id: hrow
                    required property var modelData
                    required property int index
                    objectName: "hubPluginRow_" + modelData.id
                    Layout.fillWidth: true
                    implicitHeight: hrowCol.implicitHeight + 28
                    Rectangle {
                        anchors.top: parent.top
                        width: parent.width
                        height: 1
                        color: Theme.borderRow
                    }
                    ColumnLayout {
                        id: hrowCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            FbLabel {
                                objectName: "hubPluginName"
                                text: hrow.modelData.name
                                font.pixelSize: Theme.fontSection
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                Layout.maximumWidth: 260
                            }
                            FbMono {
                                objectName: "hubPluginVersion"
                                text: hrow.modelData.version
                                color: Theme.textMuted
                            }
                            PluginOriginPill {
                                objectName: "hubPluginOrigin"
                                origin: hrow.modelData.origin
                                text: hrow.modelData.originText
                            }
                            Item { Layout.fillWidth: true }
                            FbButton {
                                objectName: "hubPluginInstall"
                                implicitHeight: 32
                                enabled: hrow.modelData.state === "install" || hrow.modelData.state === "update"
                                kind: enabled ? "primary" : "outline"
                                text: hrow.modelData.state === "update" ? qsTr("Update")
                                    : (hrow.modelData.state === "installed" ? qsTr("Installed")
                                    : (hrow.modelData.state === "busy" ? qsTr("Installing…") : qsTr("Install")))
                                onClicked: root.ctl.installHubPlugin(hrow.modelData.id, hrow.modelData.version)
                            }
                        }
                        FbLabel {
                            objectName: "hubPluginDescription"
                            visible: hrow.modelData.description !== ""
                            Layout.fillWidth: true
                            text: hrow.modelData.description
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                        FbLabel {
                            objectName: "hubPluginPermissions"
                            Layout.fillWidth: true
                            text: qsTr("Can: %1").arg(hrow.modelData.permissionsText) + (hrow.modelData.sizeText !== "" ? "  ·  " + hrow.modelData.sizeText : "")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontMeta
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
    RowLayout {
        visible: root.pathPromptOpen
        Layout.fillWidth: true
        spacing: Theme.space10
        FbField {
            id: pathField
            objectName: "pluginFolderPathField"
            Layout.fillWidth: true
            placeholderText: root.pathMode === "file" ? qsTr("Plugin file (.fbplugin) or folder path") : qsTr("Plugin folder path")
            onAccepted: applyPath.clicked()
        }
        FbButton {
            id: applyPath
            objectName: "pluginFolderApplyButton"
            implicitHeight: 44
            text: root.pathMode === "link" ? qsTr("Link folder") : qsTr("Install")
            onClicked: {
                if (root.pathMode === "link") root.ctl.linkDevFolder(pathField.text)
                else if (root.pathMode === "file") root.ctl.installFromPath(pathField.text)
                else root.ctl.installFromFolder(pathField.text)
                root.pathPromptOpen = false
            }
        }
    }
    FbLabel {
        objectName: "pluginsMessage"
        visible: root.ctl.message !== ""
        Layout.fillWidth: true
        text: root.ctl.message
        color: root.ctl.messageIsError ? Theme.error : Theme.textSecondary
        font.pixelSize: Theme.fontMeta
        wrapMode: Text.WordWrap
    }
}
