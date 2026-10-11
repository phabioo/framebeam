import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Settings > Plugins (docs/design/plugins.md 4a, minimal part): Developer mode, the installed plugins (local files),
// install from folder and, in Developer mode, link a folder. Detail page, permission dialogs and Hub browsing come later.
ColumnLayout {
    id: root
    required property PlayerController player
    readonly property PluginsController ctl: player.plugins
    readonly property var list: ctl.plugins
    property bool pathPromptOpen: false
    property string pathMode: "install"
    property string confirmUninstallId: ""
    spacing: Theme.space12

    Loader {
        id: dialogLoader
        active: false
        source: "PluginFolderDialog.qml"
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
                            FbPill {
                                objectName: "pluginOrigin"
                                small: true
                                tone: "neutral"
                                text: row.modelData.originText
                            }
                            FbPill {
                                visible: row.modelData.devLinked
                                small: true
                                tone: "neutral"
                                text: qsTr("Linked folder")
                            }
                            Item { Layout.fillWidth: true }
                            FbToggle {
                                objectName: "pluginToggle"
                                enabled: row.modelData.canToggle
                                opacity: enabled ? 1 : 0.4
                                checked: row.modelData.enabled
                                onToggled: root.ctl.setPluginEnabled(row.modelData.id, checked)
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
                text: qsTr("Plugins add panels and tools to your games. Install one from a folder on this PC.")
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
            objectName: "pluginInstallButton"
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
    RowLayout {
        visible: root.pathPromptOpen
        Layout.fillWidth: true
        spacing: Theme.space10
        FbField {
            id: pathField
            objectName: "pluginFolderPathField"
            Layout.fillWidth: true
            placeholderText: qsTr("Plugin folder path")
            onAccepted: applyPath.clicked()
        }
        FbButton {
            id: applyPath
            objectName: "pluginFolderApplyButton"
            implicitHeight: 44
            text: root.pathMode === "link" ? qsTr("Link folder") : qsTr("Install")
            onClicked: {
                if (root.pathMode === "link") root.ctl.linkDevFolder(pathField.text)
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
