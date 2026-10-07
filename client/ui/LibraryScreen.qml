import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// 3c: Library (reduced): shell, game grid, detail pane.
Rectangle {
    id: root
    required property PlayerController player
    color: Theme.bg

    // "Upload ROM": native file dialog (UploadFileDialog.qml, QtQuick.Dialogs) when the module is installed,
    // otherwise a path field. The dialog is its own file so that windeployqt sees the import.
    property bool pathPromptOpen: false
    Loader {
        id: fileDialogLoader
        active: false
        source: "UploadFileDialog.qml"
        onLoaded: item.player = root.player
    }
    function openUploadDialog() {
        fileDialogLoader.active = true
        if (fileDialogLoader.status === Loader.Ready && fileDialogLoader.item !== null) {
            fileDialogLoader.item.open()
        } else {
            root.pathPromptOpen = !root.pathPromptOpen
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: 232
            player: root.player
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 28
            Layout.leftMargin: 32
            Layout.rightMargin: 32
            spacing: 20

            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                ColumnLayout {
                    spacing: 2
                    FbLabel { text: qsTr("Library"); font.pixelSize: 26; font.weight: Font.DemiBold; font.letterSpacing: -0.4 }
                    FbLabel {
                        objectName: "libraryCount"
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                        text: root.player.library.count === root.player.library.totalCount
                              ? qsTr("%n game(s)", "", root.player.library.totalCount)
                              : qsTr("%1 of %2 games").arg(root.player.library.count).arg(root.player.library.totalCount)
                    }
                }
                Item { Layout.fillWidth: true }
                FbButton {
                    objectName: "uploadButton"
                    visible: root.player.canUpload
                    enabled: !root.player.upload.active
                    implicitHeight: 34
                    text: qsTr("Upload ROM")
                    onClicked: root.openUploadDialog()
                }
                FbField {
                    id: search
                    objectName: "searchField"
                    implicitWidth: 220
                    implicitHeight: 34
                    font.family: Qt.application.font.family
                    font.pixelSize: Theme.fontSmall
                    placeholderText: qsTr("Search…")
                    onTextChanged: root.player.library.filterText = text
                }
            }

            // Filter chips with counts (3c, D14); combined with the search field.
            RowLayout {
                objectName: "filterChips"
                Layout.fillWidth: true
                spacing: Theme.space8
                Repeater {
                    model: [
                        { name: "filterAll", value: "all", label: qsTr("All"), badge: false },
                        { name: "filterReady", value: "ready", label: qsTr("Ready"), badge: false },
                        { name: "filterAttention", value: "attention", label: qsTr("Needs attention"), badge: true },
                        { name: "filterDownload", value: "download", label: qsTr("Not downloaded"), badge: false }
                    ]
                    delegate: FbChip {
                        required property var modelData
                        objectName: modelData.name
                        text: modelData.label
                        count: root.player.library.counts[modelData.value]
                        badgeAccent: modelData.badge
                        active: root.player.library.filter === modelData.value
                        onClicked: root.player.library.filter = modelData.value
                    }
                }
                Item { Layout.fillWidth: true }
            }

            // Core warnings from the handshake (non-blocking)
            Repeater {
                model: root.player.coreWarnings
                delegate: Rectangle {
                    id: warn
                    required property var modelData
                    objectName: "coreWarning"
                    Layout.fillWidth: true
                    implicitHeight: warnText.implicitHeight + 20
                    radius: 8
                    color: Theme.warnBg
                    FbLabel {
                        id: warnText
                        anchors.fill: parent
                        anchors.margins: 10
                        anchors.leftMargin: 12
                        wrapMode: Text.WordWrap
                        verticalAlignment: Text.AlignVCenter
                        text: warn.modelData.text
                        color: Theme.warn
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }

            RowLayout {
                objectName: "uploadPathPrompt"
                Layout.fillWidth: true
                visible: root.pathPromptOpen && root.player.canUpload
                spacing: 10
                FbField {
                    id: uploadPath
                    objectName: "uploadPathField"
                    Layout.fillWidth: true
                    implicitHeight: 34
                    placeholderText: qsTr("Path of the ROM file to upload")
                    onAccepted: uploadPathButton.clicked()
                }
                FbButton {
                    id: uploadPathButton
                    objectName: "uploadPathButton"
                    implicitHeight: 34
                    text: qsTr("Upload")
                    enabled: uploadPath.text.trim() !== "" && !root.player.upload.active
                    onClicked: { root.player.uploadRom(uploadPath.text.trim()); root.pathPromptOpen = false }
                }
            }

            Rectangle {
                objectName: "uploadStatus"
                Layout.fillWidth: true
                visible: root.player.upload.active || root.player.upload.message !== ""
                implicitHeight: 34
                radius: 8
                color: root.player.upload.isError ? Theme.errorBg : Theme.surfaceRaised
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 8
                    spacing: 10
                    FbLabel {
                        Layout.fillWidth: true
                        text: root.player.upload.active
                              ? qsTr("Uploading %1 · %2 %").arg(root.player.upload.fileName).arg(Math.round(root.player.upload.progress * 100))
                              : root.player.upload.message
                        color: root.player.upload.isError ? Theme.errorText : Theme.text
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        visible: root.player.upload.active
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 4
                        radius: 2
                        color: Theme.borderInput
                        Rectangle {
                            width: parent.width * root.player.upload.progress
                            height: parent.height
                            radius: 2
                            color: Theme.accent
                        }
                    }
                    FbButton {
                        visible: !root.player.upload.active
                        kind: "link"
                        text: qsTr("Dismiss")
                        onClicked: root.player.dismissUploadMessage()
                    }
                }
            }

            Rectangle {
                objectName: "hubLinkBanner"
                Layout.fillWidth: true
                visible: root.player.sessions.available && root.player.sessions.hubLink !== "online"
                implicitHeight: 34
                radius: 8
                color: Theme.warnBg
                FbLabel {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    verticalAlignment: Text.AlignVCenter
                    text: root.player.sessions.hubLink === "connecting" ? qsTr("Connecting to the Hub for Sessions…")
                          : qsTr("Hub connection for Sessions lost · reconnecting…")
                    color: Theme.warn
                    font.pixelSize: Theme.fontSmall
                }
            }

            Rectangle {
                objectName: "sessionMessage"
                Layout.fillWidth: true
                visible: root.player.sessions.message !== ""
                implicitHeight: 34
                radius: 8
                color: root.player.sessions.messageIsError ? Theme.errorBg : Theme.surfaceRaised
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 8
                    FbLabel {
                        Layout.fillWidth: true
                        text: root.player.sessions.message
                        color: root.player.sessions.messageIsError ? Theme.errorText : Theme.text
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                    FbButton { kind: "link"; text: qsTr("Dismiss"); onClicked: root.player.sessions.dismissMessage() }
                }
            }

            SessionsSection {
                Layout.fillWidth: true
                player: root.player
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                GridView {
                    id: grid
                    objectName: "gameGrid"
                    anchors.fill: parent
                    visible: root.player.libraryState === "ready" && count > 0
                    clip: true
                    leftMargin: 4
                    topMargin: 4
                    readonly property int columns: Math.max(2, Math.floor((width - leftMargin) / 170))
                    cellWidth: Math.floor((width - leftMargin) / columns)
                    cellHeight: cellWidth + 62 + 10
                    model: root.player.library
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { }
                    add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durList } }
                    displaced: Transition { NumberAnimation { properties: "x,y"; duration: Theme.durList; easing.type: Easing.OutCubic } }
                    delegate: GameTile {
                        width: grid.cellWidth
                        height: grid.cellHeight
                        selected: gameId === root.player.selectedGameId
                        onClicked: root.player.selectGame(gameId)
                    }
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 12
                    visible: !grid.visible
                    FbSpinner {
                        Layout.alignment: Qt.AlignHCenter
                        visible: root.player.libraryState === "loading"
                        size: 22
                    }
                    FbLabel {
                        objectName: "libraryEmptyText"
                        Layout.alignment: Qt.AlignHCenter
                        color: root.player.libraryState === "error" ? Theme.error : Theme.textMuted
                        font.pixelSize: Theme.fontBody
                        text: root.player.libraryState === "loading" ? qsTr("Loading Library…")
                              : root.player.libraryState === "error" ? qsTr("Could not load Library: %1").arg(root.player.libraryError)
                              : root.player.library.totalCount === 0 ? qsTr("This hub has no games yet.")
                              : root.player.library.filter === "attention" && root.player.library.filterText === "" ? qsTr("Nothing needs attention.")
                              : qsTr("No results.")
                    }
                    FbButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: root.player.libraryState === "error"
                        busyOnClick: true
                        text: qsTr("Reload")
                        onClicked: root.player.reloadLibrary()
                    }
                }
            }
        }

        DetailPane {
            Layout.fillHeight: true
            Layout.preferredWidth: 392
            player: root.player
        }
    }
}
