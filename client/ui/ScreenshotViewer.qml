import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// Viewer of a screenshot (board 3c-1, the strip's click target): the large picture over the scrim, the date (and "Run: <name>" when
// it was played in a run and the name is known), Previous / Next, "Save as..." (a PNG file of the person's choice) and "Delete..."
// (a confirm with a danger button; the server deletes it for good). Esc and Close leave; the arrow keys step.
//   openAt(id)
// objectNames: screenshotViewer, screenshotViewerImage, screenshotViewerDate, screenshotViewerRun, screenshotViewerStatus,
// screenshotPrev, screenshotNext, screenshotSave, screenshotDelete, screenshotClose, screenshotDeleteDialog, screenshotDeleteConfirm,
// screenshotDeleteCancel.
FocusScope {
    id: root
    required property PlayerController player
    readonly property ScreenshotController shots: player.screenshots
    property string currentId: ""
    property bool shown: false
    property string status: ""
    readonly property var list: root.shots.shots
    readonly property int index: {
        for (let i = 0; i < root.list.length; ++i) if (root.list[i].id === root.currentId) return i
        return -1
    }
    readonly property var current: root.index >= 0 ? root.list[root.index] : ({})

    function openAt(id: string) {
        root.currentId = id
        root.status = ""
        root.shown = true
    }
    function close() {
        if (deleteDialog.shown) return
        root.shown = false
        root.status = ""
    }
    function step(delta: int) {
        const i = root.index + delta
        if (root.index < 0 || i < 0 || i >= root.list.length) return
        root.currentId = root.list[i].id
        root.status = ""
    }

    anchors.fill: parent
    z: 150
    visible: root.shown
    focus: root.shown
    onShownChanged: if (shown) forceActiveFocus()
    // The picture went away (deleted elsewhere): back to the page
    onIndexChanged: if (root.shown && root.index < 0 && root.currentId !== "") root.shown = false
    Keys.onEscapePressed: (event) => { root.close(); event.accepted = true }
    Keys.onLeftPressed: (event) => { root.step(-1); event.accepted = true }
    Keys.onRightPressed: (event) => { root.step(1); event.accepted = true }

    Connections {
        target: root.shots
        function onFinished(op, ok, error) {
            if (op === "remove") {
                deleteDialog.busy = false
                if (ok) { deleteDialog.close(); root.shown = false }
                else deleteDialog.error = error
            } else if (op === "save") {
                root.status = ok ? qsTr("Saved.") : error
            }
        }
    }

    Rectangle {
        objectName: "screenshotViewerScrim"
        anchors.fill: parent
        color: Theme.scrim
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onClicked: root.close(); onWheel: (wheel) => wheel.accepted = true }
    }
    Rectangle {
        id: frame
        objectName: "screenshotViewer"
        anchors.centerIn: parent
        width: Math.min(960, Math.max(0, root.width - 2 * Theme.space3))
        height: Math.min(720, Math.max(0, root.height - 2 * Theme.space3))
        color: Theme.surface
        border.width: 1
        border.color: Theme.rule
        clip: true
        MouseArea { anchors.fill: parent }   // a click in the frame is not a click on the scrim

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.space2
            spacing: Theme.space15

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 2
                    UzMono {
                        objectName: "screenshotViewerDate"
                        Layout.fillWidth: true
                        text: root.current.whenText ?? ""
                        elide: Text.ElideRight
                        color: Theme.ink2
                        font.pixelSize: Theme.roles.sectionLabel.size
                    }
                    UzLabel {
                        objectName: "screenshotViewerRun"
                        visible: (root.current.runName ?? "") !== ""
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        elide: Text.ElideRight
                        text: qsTr("Run: %1").arg(root.current.runName ?? "")
                        color: Theme.muted
                        font.pixelSize: Theme.roles.small.size
                    }
                }
                UzMono {
                    visible: root.list.length > 1 && root.index >= 0
                    text: qsTr("%1 of %2").arg(root.index + 1).arg(root.list.length)
                    color: Theme.dim
                    font.pixelSize: Theme.roles.sectionLabel.size
                }
                UzIconButton {
                    objectName: "screenshotClose"
                    glyph: "cross"
                    glyphSize: 12
                    Accessible.name: qsTr("Close")
                    onClicked: root.close()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                color: Theme.stage
                border.width: 1
                border.color: Theme.rule
                Image {
                    id: big
                    objectName: "screenshotViewerImage"
                    anchors.fill: parent
                    anchors.margins: 1
                    asynchronous: true
                    cache: false
                    fillMode: Image.PreserveAspectFit
                    smooth: false   // pixel art stays sharp when it is scaled up
                    source: root.currentId !== "" && root.shown ? "image://screenshots/" + encodeURIComponent(root.currentId) : ""
                }
                UzLabel {
                    anchors.centerIn: parent
                    visible: big.status === Image.Error
                    text: qsTr("The screenshot could not be loaded.")
                    color: Theme.muted
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space1
                UzButton {
                    objectName: "screenshotPrev"
                    text: qsTr("Previous")
                    enabled: root.index > 0
                    onClicked: root.step(-1)
                }
                UzButton {
                    objectName: "screenshotNext"
                    text: qsTr("Next")
                    enabled: root.index >= 0 && root.index < root.list.length - 1
                    onClicked: root.step(1)
                }
                UzLabel {
                    objectName: "screenshotViewerStatus"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    text: root.status
                    color: Theme.muted
                    font.pixelSize: Theme.roles.small.size
                }
                UzButton {
                    objectName: "screenshotSave"
                    text: qsTr("Save as…")
                    enabled: root.index >= 0
                    onClicked: saveLoader.open()
                }
                UzButton {
                    objectName: "screenshotDelete"
                    kind: "danger"
                    text: qsTr("Delete…")
                    enabled: root.index >= 0
                    onClicked: { deleteDialog.error = ""; deleteDialog.open() }
                }
            }
        }
    }

    Loader {
        id: saveLoader
        active: false
        source: "ScreenshotSaveDialog.qml"
        function open() {
            if (item) {
                item.screenshotId = root.currentId
                item.prepare()
                item.open()
            } else {
                active = true
            }
        }
        onLoaded: {
            item.shots = root.shots
            item.screenshotId = root.currentId
            item.prepare()
            item.open()
        }
        onStatusChanged: if (status === Loader.Error) root.status = qsTr("The file dialog is not available in this installation.")
    }

    UzDialog {
        id: deleteDialog
        objectName: "screenshotDeleteDialog"
        parent: Window.contentItem
        z: 160
        label: qsTr("Screenshots")
        title: qsTr("Delete this screenshot?")
        text: qsTr("It is deleted from the server for good. A copy you saved with “Save as…” stays.")
        actions: [
            UzButton { objectName: "screenshotDeleteCancel"; text: qsTr("Cancel"); enabled: !deleteDialog.busy; onClicked: deleteDialog.close() }
        ]
        dangerActions: [
            UzButton {
                objectName: "screenshotDeleteConfirm"
                kind: "danger"
                text: qsTr("Delete screenshot")
                busy: deleteDialog.busy
                onClicked: {
                    deleteDialog.error = ""
                    deleteDialog.busy = true
                    root.shots.remove(root.currentId)
                }
            }
        ]
        onClosed: root.forceActiveFocus()
    }
}
