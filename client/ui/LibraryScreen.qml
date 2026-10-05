import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// 3c: Library (reduced): shell, game grid, detail pane.
Rectangle {
    id: root
    required property PlayerController player
    color: Theme.bg

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
                        font.pixelSize: 13
                        text: root.player.library.count === root.player.library.totalCount
                              ? qsTr("%1 games").arg(root.player.library.totalCount)
                              : qsTr("%1 of %2 games").arg(root.player.library.count).arg(root.player.library.totalCount)
                    }
                }
                Item { Layout.fillWidth: true }
                FbField {
                    id: search
                    objectName: "searchField"
                    implicitWidth: 220
                    implicitHeight: 34
                    font.family: Qt.application.font.family
                    font.pixelSize: 13
                    placeholderText: qsTr("Search…")
                    onTextChanged: root.player.library.filterText = text
                }
            }

            RowLayout {
                spacing: 8
                Repeater {
                    model: [
                        { label: qsTr("All · %1").arg(root.player.library.totalCount), ready: false },
                        { label: qsTr("Ready · %1").arg(root.player.library.readyCount), ready: true }
                    ]
                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        readonly property bool active: root.player.library.readyOnly === modelData.ready
                        implicitHeight: 30
                        implicitWidth: chipLabel.implicitWidth + 28
                        radius: 15
                        color: active ? Theme.surfaceRaised : "transparent"
                        border.width: 1
                        border.color: active ? Theme.borderButton : Theme.borderInput
                        FbLabel {
                            id: chipLabel
                            anchors.centerIn: parent
                            text: chip.modelData.label
                            font.pixelSize: 13
                            color: chip.active ? Theme.text : Theme.textMuted
                        }
                        TapHandler { onTapped: root.player.library.readyOnly = chip.modelData.ready }
                    }
                }
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
                    FbLabel {
                        Layout.alignment: Qt.AlignHCenter
                        color: root.player.libraryState === "error" ? Theme.error : Theme.textMuted
                        font.pixelSize: 14
                        text: root.player.libraryState === "loading" ? qsTr("Loading Library…")
                              : root.player.libraryState === "error" ? qsTr("Could not load Library: %1").arg(root.player.libraryError)
                              : root.player.library.totalCount === 0 ? qsTr("This hub has no games yet.")
                              : qsTr("No results.")
                    }
                    FbButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: root.player.libraryState === "error"
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
