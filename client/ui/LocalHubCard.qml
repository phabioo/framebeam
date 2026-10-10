import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// "This PC's Hub" (Settings > Hubs): network sharing and ROM folder of the Hub that runs on this PC. Shown while the
// active Hub is that Hub.
Rectangle {
    id: root
    required property PlayerController player
    readonly property var local: player.localHub
    visible: local.onThisPc
    Layout.fillWidth: true
    implicitHeight: visible ? col.implicitHeight + 28 : 0
    radius: Theme.radius10
    color: Theme.surface
    border.width: 1
    border.color: Theme.borderCard

    property bool pathPromptOpen: false
    Loader {
        id: folderDialogLoader
        active: false
        source: "LocalFolderDialog.qml"
        onLoaded: item.local = root.local
    }
    function openFolderDialog() {
        folderDialogLoader.active = true
        if (folderDialogLoader.status === Loader.Ready && folderDialogLoader.item !== null) {
            folderDialogLoader.item.open()
        } else {
            root.pathPromptOpen = !root.pathPromptOpen
        }
    }

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 14
        spacing: Theme.space10

        FbLabel { text: qsTr("This PC's Hub"); font.pixelSize: Theme.fontSection; font.weight: Font.DemiBold }

        FbToggle {
            objectName: "localNetworkSharingToggle"
            Layout.fillWidth: true
            text: qsTr("Network sharing")
            checked: root.local.networkSharing
            enabled: !root.local.settingsBusy
            onToggled: root.local.setNetworkSharing(checked)
        }
        FbLabel {
            Layout.fillWidth: true
            text: qsTr("Off: only this PC can reach the Hub. On: other devices in your network can connect too (Windows may ask to allow it in the firewall).")
            color: Theme.textMuted
            font.pixelSize: Theme.fontMeta
            wrapMode: Text.WordWrap
        }

        Eyebrow { text: qsTr("ROM folder"); Layout.topMargin: 4 }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space10
            FbMono {
                objectName: "localImportDir"
                Layout.fillWidth: true
                text: root.local.importDir !== "" ? root.local.importDir : qsTr("No folder chosen")
                elide: Text.ElideMiddle
                color: root.local.importDir !== "" ? Theme.text : Theme.textMuted
            }
            FbButton {
                objectName: "localChooseFolderButton"
                implicitHeight: 32
                busy: root.local.settingsBusy
                enabled: !root.local.settingsBusy
                text: qsTr("Choose folder…")
                onClicked: root.openFolderDialog()
            }
        }
        RowLayout {
            visible: root.pathPromptOpen
            Layout.fillWidth: true
            spacing: Theme.space10
            FbField {
                id: pathField
                objectName: "localFolderPathField"
                Layout.fillWidth: true
                placeholderText: qsTr("Folder path, e.g. D:\\Games\\ROMs")
                onAccepted: applyPath.clicked()
            }
            FbButton {
                id: applyPath
                objectName: "localFolderApplyButton"
                implicitHeight: 44
                text: qsTr("Use folder")
                onClicked: root.local.chooseFolder(pathField.text)
            }
        }
        FbLabel {
            objectName: "localSettingsNotice"
            visible: root.local.settingsNotice !== ""
            Layout.fillWidth: true
            text: root.local.settingsNotice
            color: Theme.textSecondary
            font.pixelSize: Theme.fontMeta
            wrapMode: Text.WordWrap
        }
        FbLabel {
            objectName: "localSettingsError"
            visible: root.local.settingsError !== ""
            Layout.fillWidth: true
            text: root.local.settingsError
            color: Theme.error
            font.pixelSize: Theme.fontMeta
            wrapMode: Text.WordWrap
        }
        FbButton {
            kind: "link"
            text: qsTr("Open the Hub web page")
            onClicked: root.local.openHubWebUi()
        }
    }
}
