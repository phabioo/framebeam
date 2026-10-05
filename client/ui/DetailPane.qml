import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Detail pane (3c) for the selected game.
Rectangle {
    id: root
    required property PlayerController player
    readonly property var game: player.selectedGame
    readonly property bool hasGame: game.id !== undefined
    property bool showDetails: false

    color: Theme.bgPanel
    implicitWidth: 392

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: Theme.borderSidebar
    }

    FbLabel {
        visible: !root.hasGame
        anchors.centerIn: parent
        text: qsTr("Select a game from the Library.")
        color: Theme.textFaint
        font.pixelSize: 13
    }

    ColumnLayout {
        visible: root.hasGame
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                width: flick.width
                spacing: 18

                RowLayout {
                    spacing: 16
                    Rectangle {
                        implicitWidth: 112
                        implicitHeight: 112
                        radius: 8
                        color: Theme.tile
                        FbLabel {
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: 10
                            text: root.game.monogram || ""
                            font.pixelSize: 34
                            font.weight: Font.DemiBold
                            color: Theme.monogram
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        FbLabel {
                            objectName: "detailTitle"
                            Layout.fillWidth: true
                            text: root.game.title || ""
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                        }
                        FbLabel {
                            text: root.game.systemName || ""
                            color: Theme.textMuted
                            font.pixelSize: 13
                        }
                    }
                }

                // Status table
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Repeater {
                        model: [
                            { label: qsTr("ROM"), text: root.game.romText, tone: root.game.romTone, hint: "" },
                            { label: qsTr("Core"), text: root.game.coreText, tone: root.game.coreTone, hint: root.game.coreHint },
                            { label: qsTr("Firmware"), text: root.game.firmwareText, tone: root.game.firmwareTone, hint: root.game.firmwareHint },
                            { label: qsTr("Save"), text: root.game.saveText, tone: root.game.saveTone, hint: root.game.saveHint }
                        ]
                        delegate: Item {
                            id: row
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: rowCol.implicitHeight + 22
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: Theme.borderRow
                            }
                            ColumnLayout {
                                id: rowCol
                                anchors.fill: parent
                                anchors.topMargin: 11
                                anchors.bottomMargin: 11
                                spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    FbLabel { text: row.modelData.label; color: Theme.textMuted; font.pixelSize: 13 }
                                    Item { Layout.fillWidth: true }
                                    FbLabel {
                                        text: row.modelData.text || ""
                                        font.pixelSize: 13
                                        color: row.modelData.tone === "neutral" ? Theme.text : Theme.toneColor(row.modelData.tone)
                                    }
                                }
                                FbMono {
                                    visible: (row.modelData.hint || "") !== ""
                                    Layout.fillWidth: true
                                    text: row.modelData.hint || ""
                                    wrapMode: Text.WrapAnywhere
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }

                FbButton {
                    objectName: "detailsToggle"
                    kind: "link"
                    text: root.showDetails ? qsTr("Hide details") : qsTr("Show details (hash, size, path)")
                    onClicked: root.showDetails = !root.showDetails
                }
                ColumnLayout {
                    visible: root.showDetails
                    Layout.fillWidth: true
                    spacing: 6
                    Eyebrow { text: qsTr("SHA-256") }
                    FbMono { Layout.fillWidth: true; text: root.game.sha || ""; color: Theme.text; wrapMode: Text.WrapAnywhere; font.pixelSize: 11 }
                    Eyebrow { text: qsTr("Size"); Layout.topMargin: 4 }
                    FbMono { Layout.fillWidth: true; text: root.game.sizeText || ""; color: Theme.text; font.pixelSize: 11 }
                    Eyebrow { text: qsTr("Cache path"); Layout.topMargin: 4 }
                    FbMono { Layout.fillWidth: true; text: root.game.cachePath || ""; color: Theme.text; wrapMode: Text.WrapAnywhere; font.pixelSize: 11 }
                }

                // Start checklist
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Eyebrow { text: qsTr("Start") }
                    Repeater {
                        model: root.game.checklist || []
                        delegate: RowLayout {
                            id: step
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 12
                            Item {
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 18
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: step.modelData.state === "done" ? Theme.accent : (step.modelData.state === "error" ? Theme.error : "transparent")
                                    border.width: (step.modelData.state === "done" || step.modelData.state === "error") ? 0 : 1.5
                                    border.color: step.modelData.state === "active" ? Theme.accent : Theme.textDisabled
                                }
                            }
                            FbLabel {
                                Layout.fillWidth: true
                                text: step.modelData.label
                                font.pixelSize: 13
                                color: step.modelData.state === "pending" ? Theme.textFaint : Theme.text
                            }
                            FbMono {
                                visible: (step.modelData.meta || "") !== ""
                                text: step.modelData.meta || ""
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                Rectangle {
                    visible: (root.game.error || "") !== ""
                    objectName: "startError"
                    Layout.fillWidth: true
                    implicitHeight: errText.implicitHeight + 24
                    radius: 8
                    color: Theme.errorBg
                    FbLabel {
                        id: errText
                        anchors.fill: parent
                        anchors.margins: 12
                        text: root.game.error || ""
                        wrapMode: Text.WordWrap
                        font.pixelSize: 13
                        color: Theme.errorText
                    }
                }
            }
        }

        FbButton {
            objectName: "playButton"
            Layout.fillWidth: true
            implicitHeight: 48
            kind: "primary"
            font.pixelSize: 15
            text: root.game.playLabel || qsTr("Play")
            enabled: root.game.canPlay === true
            onClicked: root.player.playSelected()
        }
    }
}
