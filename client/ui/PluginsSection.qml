import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// Settings > Plugins (round 3, 4a-3 / 4a-4): installed plugins first (58 px rows: monogram, name, version, origin pill,
// one subtitle line, Update, toggle, "›" for the detail sheet), then "Available on <Hub>" with Install. Installing a plugin
// that needs another one offers both; turning on an unverified plugin or one whose dependency is off asks first.
// Developer mode and the local folder installs sit behind "Developer settings".
ColumnLayout {
    id: root
    required property PlayerController player
    readonly property PluginsController ctl: player.plugins
    readonly property var list: ctl.plugins
    property bool pathPromptOpen: false
    property string pathMode: "file"   // file | install (folder) | link
    property string confirmUninstallId: ""
    property string confirmEnableId: ""
    property string hubName: ""
    readonly property Item overlayParent: Window.window ? Window.window.contentItem : null   // popups are placed in window coordinates
    property string confirmInstallId: ""      // Hub plugin with missing dependencies: "Install both?"
    property string sheetPluginId: ""
    property bool devOpen: false
    property string checkedAt: ""             // ISO time of the last successful Hub list
    readonly property var available: ctl.hubPlugins.filter(p => p.installedVersion === "")
    readonly property int firstAvailableIndex: {
        const l = ctl.hubPlugins
        for (let i = 0; i < l.length; ++i) if (l[i].installedVersion === "") return i
        return -1
    }
    readonly property var confirmInstallPlugin: {
        for (const p of ctl.hubPlugins) if (p.id === root.confirmInstallId) return p
        return null
    }
    readonly property var confirmEnablePlugin: {
        for (const p of list) if (p.id === root.confirmEnableId) return p
        return null
    }
    readonly property string hubLabel: hubName !== "" ? hubName : qsTr("your Hub")
    Connections {
        target: root.ctl
        function onHubPluginsChanged() { if (root.ctl.hubStatus === "ready") root.checkedAt = new Date().toISOString() }
    }
    Component.onCompleted: if (root.ctl.hubSupported && root.ctl.hubStatus === "idle") root.ctl.refreshHubPlugins()
    function monogram(name: string): string {
        const w = name.split(/[^A-Za-z0-9]+/).filter(s => s.length > 0)
        return ((w[0] ? w[0][0] : "?") + (w[1] ? w[1][0] : "")).toUpperCase()
    }
    function requestEnable(p) {
        if (p.needsConfirm || p.depsOff.length > 0) root.confirmEnableId = p.id
        else root.ctl.setPluginEnabled(p.id, true)
    }
    property string highlightId: ""            // row shown with an accent border for a moment ("Plugin settings" from a plugin screen)
    Timer { id: highlightTimer; interval: 3000; onTriggered: root.highlightId = "" }
    onHighlightIdChanged: if (highlightId !== "") highlightTimer.restart()
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
        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
        Layout.fillWidth: true
        text: qsTr("If a plugin fails it is paused; your game keeps running.")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
        wrapMode: Text.WordWrap
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 4
        Eyebrow { objectName: "pluginsInstalledTitle"; Layout.fillWidth: true; text: qsTr("Installed · %1").arg(root.list.length) }
        FbButton {
            textFormat: Text.PlainText
            objectName: "pluginInstallButton"
            kind: "link"
            text: qsTr("Install from file…")
            onClicked: root.openFileDialog()
        }
    }

    // ---- installed list
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
            // The model is the row count: rows are updated in place, not rebuilt on every refresh (a rebuilt Repeater
            // inside a Qt 6.4 layout can leave the layout with a deleted item).
            Repeater {
                model: root.list.length
                delegate: Item {
                    id: row
                    readonly property var modelData: root.list[index] || ({})
                    required property int index
                    objectName: "pluginRow_" + (modelData.id || "")
                    Layout.fillWidth: true
                    implicitHeight: Math.max(58, rowLine.implicitHeight + 16 + (problemLabel.visible ? problemLabel.implicitHeight + 4 : 0))
                    Rectangle {
                        objectName: "pluginRowHighlight"
                        visible: root.highlightId === row.modelData.id
                        anchors.fill: parent
                        anchors.margins: 1
                        radius: Theme.radius8
                        color: "transparent"
                        border.width: 2
                        border.color: Theme.accent
                    }
                    Rectangle {
                        visible: row.index > 0
                        anchors.top: parent.top
                        width: parent.width
                        height: 1
                        color: Theme.borderRow
                    }
                    TapHandler {
                        onTapped: point => {
                            // The open sheet covers part of the list (a wide font can put a row under its buttons): taps on the
                            // sheet must not also switch the row below it.
                            if (sheet.opened) {
                                const p = sheet.contentItem.mapFromItem(null, point.scenePosition)
                                if (p.x >= 0 && p.y >= 0 && p.x < sheet.contentItem.width && p.y < sheet.contentItem.height) {
                                    return
                                }
                            }
                            root.sheetPluginId = row.modelData.id
                        }
                    }
                    RowLayout {
                        id: rowLine
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 8
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 12
                        Rectangle {
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            Layout.alignment: Qt.AlignTop
                            radius: Theme.radius8
                            color: Theme.neutralPillBg
                            Text {
                                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                                anchors.centerIn: parent
                                text: root.monogram(row.modelData.name)
                                color: Theme.text
                                font.pixelSize: Theme.fontSmall
                                font.weight: Font.DemiBold
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 8
                                FbLabel {
                                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                                    objectName: "pluginName"
                                    text: row.modelData.name
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: 260
                                }
                                FbMono {
                                    textFormat: Text.PlainText
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
                                    textFormat: Text.PlainText
                                    visible: row.modelData.devLinked
                                    small: true
                                    tone: "neutral"
                                    text: qsTr("Linked folder")
                                }
                                Item { Layout.fillWidth: true }
                            }
                            FbLabel {
                                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                                objectName: "pluginSubtitle"
                                Layout.fillWidth: true
                                text: row.modelData.subtitle
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontMeta
                                elide: Text.ElideRight
                            }
                        }
                        FbButton {
                            textFormat: Text.PlainText
                            objectName: "pluginUpdate"
                            visible: row.modelData.updateVersion !== ""
                            Layout.alignment: Qt.AlignVCenter
                            implicitHeight: 32
                            text: qsTr("Update to %1").arg(row.modelData.updateVersion)
                            onClicked: root.ctl.installHubPlugin(row.modelData.id, row.modelData.updateVersion)
                        }
                        FbToggle {
                            id: toggle
                            objectName: "pluginToggle"
                            Layout.alignment: Qt.AlignVCenter
                            enabled: row.modelData.canToggle
                            opacity: enabled ? 1 : 0.4
                            checked: row.modelData.enabled
                            onToggled: {
                                if (checked) {
                                    // Unverified or a dependency that is off: ask first (the toggle stays off until confirmed).
                                    checked = Qt.binding(function() { return row.modelData.enabled })
                                    root.requestEnable(row.modelData)
                                } else {
                                    root.ctl.setPluginEnabled(row.modelData.id, false)
                                }
                            }
                        }
                        FbButton {
                            textFormat: Text.PlainText
                            objectName: "pluginOpen"
                            Layout.alignment: Qt.AlignVCenter
                            kind: "link"
                            text: "›"
                            onClicked: root.sheetPluginId = row.modelData.id
                        }
                    }
                    FbLabel {
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        id: problemLabel
                        objectName: "pluginProblems"
                        visible: row.modelData.problemsText !== ""
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: rowLine.bottom
                        anchors.leftMargin: 16 + 34 + 12
                        anchors.rightMargin: 16
                        text: "! " + row.modelData.problemsText
                        color: Theme.pluginWarn
                        font.pixelSize: Theme.fontMeta
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    // ---- empty state: the Hub list below is already loaded, so say what it offers (P19)
    Rectangle {
        objectName: "pluginsEmpty"
        visible: root.list.length === 0
        Layout.fillWidth: true
        implicitHeight: emptyCol.implicitHeight + 40
        radius: Theme.radius10
        color: Theme.surface
        border.width: 1
        border.color: Theme.borderCard
        ColumnLayout {
            id: emptyCol
            anchors.centerIn: parent
            width: parent.width - 48
            spacing: 6
            FbLabel { textFormat: Text.PlainText; Layout.alignment: Qt.AlignHCenter; text: qsTr("No plugins installed"); font.pixelSize: Theme.fontSection; font.weight: Font.DemiBold }
            FbLabel {
                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                objectName: "pluginsEmptyText"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: {
                    if (!root.ctl.hubSupported)
                        return qsTr("Plugins add panels and tools to your games. Install a .fbplugin file from this PC.")
                    const n = root.ctl.hubPlugins.length
                    if (n === 0) return qsTr("%1 offers no plugins yet. Its admin chooses what is offered. You can also install a .fbplugin file from this PC.").arg(root.hubLabel)
                    return qsTr("%1 offers %2. They are listed below under Available on %1.")
                        .arg(root.hubLabel).arg(Theme.plural(n, qsTr("plugin"), qsTr("plugins")))
                }
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
            }
        }
    }

    // ---- available on the Hub
    RowLayout {
        visible: root.ctl.hubSupported
        Layout.fillWidth: true
        Layout.topMargin: 10
        spacing: 10
        Eyebrow { objectName: "hubPluginsTitle"; Layout.fillWidth: true; text: qsTr("Available on %1 · %2").arg(root.hubLabel).arg(root.available.length) }
        FbLabel {
            textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
            objectName: "hubPluginsStatus"
            text: root.ctl.hubStatus === "loading" ? qsTr("Loading…")
                  : (root.checkedAt !== "" ? qsTr("Checked %1").arg(root.player.whenText(root.checkedAt)) : "")
            visible: text !== ""
            color: Theme.textMuted
            font.pixelSize: Theme.fontMeta
        }
        FbButton {
            textFormat: Text.PlainText
            objectName: "hubPluginsRefresh"
            kind: "link"
            text: qsTr("Refresh")
            onClicked: root.ctl.refreshHubPlugins()
        }
    }
    Rectangle {
        objectName: "hubPluginsPanel"
        visible: root.ctl.hubSupported
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
                objectName: "hubPluginsError"
                visible: root.ctl.hubStatus === "failed"
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    FbLabel { textFormat: Text.PlainText; text: qsTr("Could not reach %1").arg(root.hubLabel); font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold }
                    FbLabel {
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        Layout.fillWidth: true
                        text: qsTr("Available plugins could not be loaded. Installed plugins keep working.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontMeta
                        wrapMode: Text.WordWrap
                    }
                }
                FbButton { textFormat: Text.PlainText; objectName: "hubPluginsRetry"; implicitHeight: 32; text: qsTr("Retry"); onClicked: root.ctl.refreshHubPlugins() }
            }
            FbLabel {
                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                objectName: "hubPluginsEmpty"
                visible: root.available.length === 0 && root.ctl.hubStatus === "ready"
                Layout.fillWidth: true
                Layout.margins: 16
                text: root.ctl.hubPlugins.length === 0 ? qsTr("%1 offers no plugins yet. Its admin chooses what is offered.").arg(root.hubLabel)
                                                       : qsTr("Everything %1 offers is installed.").arg(root.hubLabel)
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
            // The full offer list; rows that are installed stay hidden (the list keeps its identity, so the rows are not
            // rebuilt on every refresh of the installed plugins).
            Repeater {
                model: root.ctl.hubPlugins.length
                delegate: Item {
                    id: hrow
                    readonly property var modelData: root.ctl.hubPlugins[index] || ({})
                    required property int index
                    readonly property bool shown: modelData.installedVersion === ""
                    objectName: shown ? "hubPluginRow_" + modelData.id : ""
                    visible: shown
                    Layout.fillWidth: true
                    Layout.preferredHeight: shown ? 58 : 0
                    implicitHeight: 58
                    Rectangle {
                        visible: hrow.index > root.firstAvailableIndex
                        anchors.top: parent.top
                        width: parent.width
                        height: 1
                        color: Theme.borderRow
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 12
                        Rectangle {
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            radius: Theme.radius8
                            color: Theme.neutralPillBg
                            Text {
                                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                                anchors.centerIn: parent
                                text: root.monogram(hrow.modelData.name)
                                color: Theme.text
                                font.pixelSize: Theme.fontSmall
                                font.weight: Font.DemiBold
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                FbLabel {
                                    textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                                    objectName: "hubPluginName"
                                    text: hrow.modelData.name
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: 260
                                }
                                FbMono { textFormat: Text.PlainText; objectName: "hubPluginVersion"; text: hrow.modelData.version; color: Theme.textMuted }
                                PluginOriginPill {
                                    objectName: "hubPluginOrigin"
                                    origin: hrow.modelData.origin
                                    text: hrow.modelData.originText
                                }
                                Item { Layout.fillWidth: true }
                            }
                            FbLabel {
                                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                                objectName: "hubPluginPermissions"
                                Layout.fillWidth: true
                                text: qsTr("Can: %1").arg(hrow.modelData.permissionsText) + (hrow.modelData.sizeText !== "" ? " · " + hrow.modelData.sizeText : "")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontMeta
                                elide: Text.ElideRight
                            }
                        }
                        FbButton {
                            textFormat: Text.PlainText
                            objectName: "hubPluginInstall"
                            implicitHeight: 32
                            enabled: hrow.modelData.state === "install"
                            text: hrow.modelData.state === "busy" ? qsTr("Installing…") : qsTr("Install")
                            onClicked: {
                                if (hrow.modelData.needsInstall.length > 0) root.confirmInstallId = hrow.modelData.id
                                else root.ctl.installHubPlugin(hrow.modelData.id, hrow.modelData.version)
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- plugin language
    SettingsRow {
        objectName: "pluginLanguageRow"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        resetMode: "none"
        label: qsTr("Plugin language")
        description: qsTr("Language of plugin texts. Auto uses the 3DS system language for 3DS games, otherwise the language of this computer. Applies when the next game starts.")
        descriptionName: "pluginLanguageHint"
        ctrl: "select"
        controlName: "pluginLanguageSelect"
        values: root.ctl.pluginLanguageOptions
        current: root.ctl.pluginLanguage
        onValuePicked: value => root.ctl.pluginLanguage = value
    }

    // ---- developer settings
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 10
        FbLabel {
            textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
            Layout.fillWidth: true
            text: qsTr("Developer mode and local folders:")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }
        FbButton {
            textFormat: Text.PlainText
            objectName: "pluginDeveloperSettings"
            kind: "link"
            text: root.devOpen ? qsTr("Hide developer settings") : qsTr("Developer settings")
            onClicked: root.devOpen = !root.devOpen
        }
    }
    ColumnLayout {
        visible: root.devOpen
        Layout.fillWidth: true
        spacing: Theme.space12
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
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space10
            FbButton {
                textFormat: Text.PlainText
                objectName: "pluginInstallFolderButton"
                implicitHeight: 36
                text: qsTr("Install from folder…")
                onClicked: root.openFolderDialog("install")
            }
            FbButton {
                textFormat: Text.PlainText
                objectName: "pluginLinkButton"
                visible: root.ctl.developerMode
                implicitHeight: 36
                text: qsTr("Link folder for development…")
                onClicked: root.openFolderDialog("link")
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
            textFormat: Text.PlainText
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
        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
        objectName: "pluginsMessage"
        visible: root.ctl.message !== ""
        Layout.fillWidth: true
        text: root.ctl.message
        color: root.ctl.messageIsError ? Theme.error : Theme.textSecondary
        font.pixelSize: Theme.fontMeta
        wrapMode: Text.WordWrap
    }

    // ---- detail sheet
    PluginDetailSheet {
        id: sheet
        player: root.player
        hubName: root.hubName
        overlayParent: root.overlayParent
        pluginId: root.sheetPluginId
        onEnableRequested: id => root.confirmEnableId = id
        onClosed: root.sheetPluginId = ""
    }
    onSheetPluginIdChanged: if (sheetPluginId !== "") sheet.open()

    // ---- dialogs (400 wide, centered): install with a dependency, turn on
    component PluginDialog: Popup {
        id: dlg
        property string contentName: ""
        dim: true
        property alias bodyItem: slot.data
        parent: root.overlayParent
        anchors.centerIn: parent
        width: 400
        modal: true
        padding: 20
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            radius: Theme.radius12
            color: Theme.popupBg
            border.width: 1
            border.color: Theme.borderPopup
        }
        contentItem: ColumnLayout {
            id: slot
            objectName: dlg.contentName
            spacing: 12
        }
    }

    PluginDialog {
        id: installDialog
        contentName: "pluginInstallDialog"
        visible: root.confirmInstallPlugin !== null
        onClosed: root.confirmInstallId = ""
        bodyItem: [
            FbLabel {
                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                Layout.fillWidth: true
                text: root.confirmInstallPlugin ? qsTr("Install %1?").arg(root.confirmInstallPlugin.name) : ""
                font.pixelSize: Theme.fontSection
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            },
            FbLabel {
                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                Layout.fillWidth: true
                text: root.confirmInstallPlugin
                      ? (root.confirmInstallPlugin.origin === "unverified" ? root.confirmInstallPlugin.originText + ". " : "")
                        + qsTr("It can: %1.").arg(root.confirmInstallPlugin.permissionsText) : ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            },
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: depRow.implicitHeight + 20
                radius: Theme.radius8
                color: Theme.surfaceRaised
                RowLayout {
                    id: depRow
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10
                    Text { textFormat: Text.PlainText; text: "✓"; color: Theme.accent; font.pixelSize: Theme.fontBody }
                    FbLabel {
                        textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                        objectName: "pluginInstallDependency"
                        Layout.fillWidth: true
                        text: {
                            const p = root.confirmInstallPlugin
                            if (!p) return ""
                            const d = p.needsInstall.map(x => x.name + " " + x.version).join(", ")
                            return qsTr("Also install %1 · needed by %2").arg(d).arg(p.name)
                        }
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontSmall
                    }
                }
            },
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Item { Layout.fillWidth: true }
                FbButton { textFormat: Text.PlainText; objectName: "pluginInstallCancel"; implicitHeight: 34; text: qsTr("Cancel"); onClicked: installDialog.close() }
                FbButton {
                    textFormat: Text.PlainText
                    objectName: "pluginInstallBoth"
                    implicitHeight: 34
                    kind: "primary"
                    text: qsTr("Install both")
                    onClicked: {
                        const p = root.confirmInstallPlugin
                        installDialog.close()
                        if (p) root.ctl.installHubPluginWithDependencies(p.id, p.version)
                    }
                }
            }
        ]
    }

    PluginDialog {
        id: enableDialog
        contentName: "pluginEnableConfirm"
        visible: root.confirmEnablePlugin !== null
        onClosed: root.confirmEnableId = ""
        bodyItem: [
            FbLabel {
                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                Layout.fillWidth: true
                text: root.confirmEnablePlugin ? qsTr("Turn on %1?").arg(root.confirmEnablePlugin.name) : ""
                font.pixelSize: Theme.fontSection
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            },
            FbLabel {
                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                objectName: "pluginUnverifiedWarning"
                visible: root.confirmEnablePlugin !== null && root.confirmEnablePlugin.needsConfirm
                Layout.fillWidth: true
                text: qsTr("Uzume has not checked this plugin. Turn it on only if you trust the admin of your Hub and where the file came from.")
                color: Theme.pluginWarnText
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            },
            FbLabel {
                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                objectName: "pluginEnablePermissions"
                Layout.fillWidth: true
                text: root.confirmEnablePlugin ? qsTr("It will be able to: %1").arg(root.confirmEnablePlugin.permissionsText) : ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            },
            FbLabel {
                textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                objectName: "pluginEnableDependency"
                visible: root.confirmEnablePlugin !== null && root.confirmEnablePlugin.depsOff.length > 0
                Layout.fillWidth: true
                text: root.confirmEnablePlugin ? qsTr("It needs %1, which is off. Both turn on.").arg(root.confirmEnablePlugin.depsOff.join(", ")) : ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            },
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Item { Layout.fillWidth: true }
                FbButton { textFormat: Text.PlainText; objectName: "pluginEnableCancel"; implicitHeight: 34; text: qsTr("Cancel"); onClicked: enableDialog.close() }
                FbButton {
                    textFormat: Text.PlainText
                    objectName: "pluginEnableConfirmButton"
                    implicitHeight: 34
                    kind: "primary"
                    text: root.confirmEnablePlugin && root.confirmEnablePlugin.depsOff.length > 0 ? qsTr("Turn on both") : qsTr("Turn on")
                    onClicked: {
                        const id = root.confirmEnableId
                        enableDialog.close()
                        root.ctl.setPluginEnabled(id, true)
                    }
                }
            }
        ]
    }
}
