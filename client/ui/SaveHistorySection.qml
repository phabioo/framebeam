import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Detail pane: save slot picker (Hub saves_v1), save history with "Restore" and "Create snapshot" (saves_v2).
ColumnLayout {
    id: root
    required property PlayerController player
    readonly property SaveHistoryController hist: player.saveHistory
    property string newSlotError: ""

    objectName: "saveSection"
    visible: hist.slotsAvailable
    spacing: 10

    // "Upload save file...": native file dialog (SaveFileDialog.qml, QtQuick.Dialogs) when the module is installed,
    // otherwise a path field.
    property bool pathPromptOpen: false
    Loader {
        id: saveFileDialogLoader
        active: false
        source: "SaveFileDialog.qml"
        onLoaded: item.history = root.hist
    }
    function openSaveFileDialog(field) {
        if (root.pathPromptOpen) {
            root.hist.requestUploadFile(field.text)
            return
        }
        saveFileDialogLoader.active = true
        if (saveFileDialogLoader.status === Loader.Ready && saveFileDialogLoader.item !== null) {
            saveFileDialogLoader.item.open()
        } else {
            root.pathPromptOpen = true
        }
    }

    function createSlot() {
        root.newSlotError = root.hist.createSlot(newSlotField.text)
        if (root.newSlotError === "") {
            newSlotField.text = ""
            newSlotRow.visible = false
        }
    }

    // Slots
    Eyebrow { text: qsTr("Save slot") }
    RowLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 1
        spacing: 8
        FbSelect {
            objectName: "slotPicker"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            enabled: !root.hist.gameRunning && !root.hist.busy
            model: root.hist.slotOptions
            current: root.hist.slot
            onPicked: (v) => root.hist.selectSlot(v)
        }
        FbButton {
            objectName: "newSlotButton"
            implicitHeight: 34
            text: qsTr("New slot")
            enabled: !root.hist.gameRunning
            onClicked: {
                root.newSlotError = ""
                newSlotRow.visible = !newSlotRow.visible
                if (newSlotRow.visible) newSlotField.forceActiveFocus()
            }
        }
    }
    RowLayout {
        id: newSlotRow
        objectName: "newSlotRow"
        visible: false
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 1
        spacing: 8
        FbField {
            id: newSlotField
            objectName: "newSlotField"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            implicitHeight: 34
            placeholderText: qsTr("slot-name")
            maximumLength: 64
            onTextChanged: root.newSlotError = ""
            onAccepted: root.createSlot()
        }
        FbButton {
            objectName: "newSlotCreate"
            implicitHeight: 34
            kind: "primary"
            text: qsTr("Create")
            onClicked: root.createSlot()
        }
    }
    FbLabel {
        objectName: "newSlotError"
        visible: root.newSlotError !== ""
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 1
        text: root.newSlotError
        color: Theme.error
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
    FbLabel {
        visible: root.hist.gameRunning
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 1
        text: qsTr("The slot cannot be changed while this game runs.")
        color: Theme.textFaint
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    // History (saves_v2)
    ColumnLayout {
        objectName: "historyBlock"
        visible: root.hist.available
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 1
        Layout.topMargin: 8
        spacing: 8

        // Anchored instead of a RowLayout: the title elides, the button stays pinned to the right edge of the pane.
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            implicitHeight: Math.max(historyTitle.implicitHeight, historyRefresh.implicitHeight)
            Eyebrow {
                id: historyTitle
                anchors.left: parent.left
                anchors.right: historyRefresh.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Save history")
            }
            FbButton {
                id: historyRefresh
                objectName: "historyRefresh"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                kind: "link"
                text: root.hist.loading ? qsTr("Loading…") : qsTr("Refresh")
                enabled: !root.hist.loading
                onClicked: root.hist.refresh()
            }
        }

        Rectangle {
            objectName: "historyMessage"
            visible: root.hist.message !== ""
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            implicitHeight: msgRow.implicitHeight + 16
            radius: 8
            color: root.hist.messageIsError ? Theme.errorBg : Theme.okBg
            RowLayout {
                id: msgRow
                anchors.fill: parent
                anchors.margins: 8
                FbLabel {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    text: root.hist.message
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: root.hist.messageIsError ? Theme.errorText : Theme.ok
                }
                FbButton { kind: "link"; text: qsTr("Dismiss"); onClicked: root.hist.dismissMessage() }
            }
        }

        FbLabel {
            objectName: "historyEmpty"
            visible: !root.hist.loading && root.hist.history.length === 0
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            text: qsTr("No saved versions yet.")
            color: Theme.textFaint
            font.pixelSize: 12
        }

        Repeater {
            model: root.hist.history
            delegate: Rectangle {
                id: row
                clip: true
                required property var modelData
                objectName: "historyRow"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                implicitHeight: rowLayout.implicitHeight + 20
                radius: 8
                color: Theme.surface
                border.width: 1
                border.color: Theme.borderCard
                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        spacing: 2
                        FbLabel {
                            objectName: "historyTitle"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            text: row.modelData.label !== "" ? qsTr("%1 · %2").arg(row.modelData.versionText).arg(row.modelData.label)
                                                             : row.modelData.versionText
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        FbLabel {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            text: row.modelData.reasonText
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        FbMono {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            text: qsTr("%1 · %2").arg(row.modelData.device).arg(row.modelData.when)
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                    FbButton {
                        objectName: "restoreButton"
                        implicitHeight: 32
                        text: qsTr("Restore")
                        enabled: root.hist.restoreBlockReason === "" && !root.hist.busy
                        onClicked: root.hist.requestRestore(row.modelData.version)
                    }
                }
            }
        }

        FbLabel {
            objectName: "restoreBlockReason"
            visible: root.hist.restoreBlockReason !== "" && (root.hist.history.length > 0 || root.hist.canUploadFile)
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            text: qsTr("Restore and upload are off: %1").arg(root.hist.restoreBlockReason)
            color: Theme.textFaint
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        // Upload a local save file (saves_v4)
        RowLayout {
            objectName: "uploadSaveRow"
            visible: root.hist.canUploadFile
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            spacing: 8
            FbField {
                id: uploadPath
                objectName: "uploadPathField"
                visible: root.pathPromptOpen
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                implicitHeight: 34
                placeholderText: qsTr("Path to the save file")
                onAccepted: root.hist.requestUploadFile(text)
            }
            Item { visible: !root.pathPromptOpen; Layout.fillWidth: true }
            FbButton {
                objectName: "uploadSaveButton"
                implicitHeight: 34
                text: root.pathPromptOpen ? qsTr("Choose") : qsTr("Upload save file…")
                enabled: root.hist.restoreBlockReason === "" && !root.hist.busy
                onClicked: root.openSaveFileDialog(uploadPath)
            }
        }

        // Snapshot
        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            Layout.topMargin: 4
            spacing: 8
            FbField {
                id: snapshotLabel
                objectName: "snapshotLabel"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                implicitHeight: 34
                placeholderText: qsTr("Label (optional)")
                maximumLength: 64
                onAccepted: if (root.hist.canSnapshot) root.hist.createSnapshot(text)
            }
            FbButton {
                objectName: "snapshotButton"
                implicitHeight: 34
                text: qsTr("Create snapshot")
                enabled: root.hist.canSnapshot
                onClicked: {
                    root.hist.createSnapshot(snapshotLabel.text)
                    snapshotLabel.text = ""
                }
            }
        }
    }
}
