import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Detail pane (3c) for the selected game.
Rectangle {
    id: root
    objectName: "detailPane"
    required property PlayerController player
    readonly property var game: player.selectedGame
    readonly property bool hasGame: game.id !== undefined
    // "overview" (status, SAVE summary, start checklist) or "saves" (3c-3 saves view in the same 392 column)
    property string mode: "overview"
    readonly property SaveHistoryController hist: player.saveHistory
    // A restore, delete or upload confirmation is open, or an upload runs: Play is disabled (decision af).
    readonly property bool confirming: hist.confirmationOpen
    readonly property string gameId: game.id === undefined ? "" : game.id
    onGameIdChanged: {
        root.leaveSaves()
        if (gameId !== "") fadeIn.restart()
    }
    function openSaves(upload) {
        root.mode = "saves"
        savesLoader.active = true
        if (upload === true) Qt.callLater(() => { if (savesLoader.item !== null) savesLoader.item.openUpload() })
        else Qt.callLater(() => { if (savesLoader.item !== null) savesLoader.item.forceActiveFocus() })
    }
    function leaveSaves() {
        if (savesLoader.item !== null) savesLoader.item.cancelAll()
        root.mode = "overview"
    }
    NumberAnimation { id: fadeIn; target: flick; property: "opacity"; from: 0.35; to: 1; duration: Theme.durPage }

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
        font.pixelSize: Theme.fontSmall
    }

    ColumnLayout {
        visible: root.hasGame
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        Flickable {
            id: flick
            visible: root.mode === "overview"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            contentWidth: width
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                width: flick.width
                spacing: 18

                RowLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: Theme.space16
                    Rectangle {
                        implicitWidth: 112
                        implicitHeight: 112
                        radius: Theme.radius8
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
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        spacing: Theme.space6
                        FbPill {
                            objectName: "detailPill"
                            Layout.minimumWidth: 0
                            Layout.maximumWidth: parent.width
                            tone: root.game.pillTone || "neutral"
                            text: root.game.pillText || ""
                        }
                        FbLabel {
                            objectName: "detailTitle"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            wrapMode: Text.Wrap
                            text: root.game.title || ""
                            font.pixelSize: Theme.fontDetail
                            font.weight: Font.DemiBold
                        }
                        FbLabel {
                            objectName: "detailSystem"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            text: (root.game.coreVersionText || "") !== ""
                                  ? qsTr("%1 · %2 %3").arg(root.game.systemName || "").arg(root.game.coreLabelText || "").arg(root.game.coreVersionText)
                                  : ((root.game.coreLabelText || "") !== "" ? qsTr("%1 · %2").arg(root.game.systemName || "").arg(root.game.coreLabelText)
                                                                              : (root.game.systemName || ""))
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                }

                // Status table
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 0
                    Repeater {
                        model: [
                            { label: qsTr("ROM"), text: root.game.romText, tone: root.game.romTone, hint: "" },
                            { label: qsTr("Core"), text: root.game.coreText, tone: root.game.coreTone, hint: root.game.coreHint },
                            { label: qsTr("Firmware"), text: root.game.firmwareText, tone: root.game.firmwareTone, hint: root.game.firmwareHint }
                        ]
                        delegate: Item {
                            id: row
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
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
                                    Layout.minimumWidth: 0
                                    spacing: 12
                                    FbLabel { text: row.modelData.label; color: Theme.textMuted; font.pixelSize: 13 }
                                    FbLabel {
                                        // The value takes the rest of the row and wraps: a long status text must never widen the pane.
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        horizontalAlignment: Text.AlignRight
                                        wrapMode: Text.Wrap
                                        objectName: "detailRowValue"
                                        text: (row.modelData.tone === "ok" ? "✓ " : (row.modelData.tone === "warn" ? "▲ " : (row.modelData.tone === "error" ? "✕ " : "")))
                                              + (row.modelData.text || "")
                                        font.pixelSize: Theme.fontSmall
                                        color: row.modelData.tone === "neutral" ? Theme.text : Theme.toneColor(row.modelData.tone)
                                    }
                                }
                                FbMono {
                                    visible: (row.modelData.hint || "") !== ""
                                    Layout.fillWidth: true
                                    text: row.modelData.hint || ""
                                    wrapMode: Text.WrapAnywhere
                                    font.pixelSize: Theme.fontMono
                                }
                            }
                        }
                    }
                }

                SaveSummary {
                    player: root.player
                    Layout.maximumWidth: content.width
                    onManage: root.openSaves(false)
                    onUploadRequested: root.openSaves(true)
                }

                FbButton {
                    objectName: "firmwareRecheck"
                    visible: root.game.firmwareBlocked === true
                    kind: "link"
                    text: qsTr("Check firmware again")
                    onClicked: root.player.recheckFirmware()
                }
                // Start checklist
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 10
                    Eyebrow { text: qsTr("Start") }
                    Repeater {
                        model: root.game.checklist || []
                        delegate: RowLayout {
                            id: step
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
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
                                elide: Text.ElideRight
                                Layout.minimumWidth: 60
                                font.pixelSize: Theme.fontSmall
                                color: step.modelData.state === "pending" ? Theme.textFaint : Theme.text
                            }
                            FbMono {
                                visible: (step.modelData.meta || "") !== ""
                                text: step.modelData.meta || ""
                                elide: Text.ElideRight
                                Layout.maximumWidth: 150
                                font.pixelSize: Theme.fontMono
                            }
                        }
                    }
                }

                Rectangle {
                    visible: (root.game.error || "") !== ""
                    objectName: "startError"
                    Layout.fillWidth: true
                    implicitHeight: errText.implicitHeight + 24
                    radius: Theme.radius8
                    color: Theme.errorBg
                    FbLabel {
                        id: errText
                        anchors.fill: parent
                        anchors.margins: 12
                        text: root.game.error || ""
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontSmall
                        color: Theme.errorText
                    }
                }
            }
        }

        Loader {
            id: savesLoader
            active: false
            visible: root.mode === "saves"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            sourceComponent: SavesView {
                player: root.player
                onBack: root.leaveSaves()
            }
        }

        FbButton {
            objectName: "playButton"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: 48
            kind: "primary"
            font.pixelSize: Theme.fontSection
            busy: root.game.busy === true
            busyOnClick: true
            text: root.game.playLabel || qsTr("Play")
            enabled: root.game.canPlay === true && !root.confirming
            onClicked: root.game.running === true ? root.player.resumeGame() : root.player.playSelected()
        }
        FbButton {
            objectName: "playShareButton"
            visible: root.player.sessions.available || root.game.running === true
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: 44
            text: root.game.running === true ? qsTr("Quit game") : qsTr("Play and share Session")
            enabled: root.game.running === true || (root.game.canPlay === true && !root.confirming)
            onClicked: root.game.running === true ? root.player.quitGame() : root.player.playAndShareSelected()
        }
        FbLabel {
            objectName: "shareVisibilityHint"
            visible: root.player.sessions.available && root.game.running !== true
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textFaint
            font.pixelSize: Theme.fontMeta
            text: qsTr("Visibility: %1 · change it in the Session panel")
                  .arg(root.player.sessions.visibility === "private" ? qsTr("Private")
                       : root.player.sessions.visibility === "invite_only" ? qsTr("Invite only") : qsTr("Hub users"))
        }
    }
}
