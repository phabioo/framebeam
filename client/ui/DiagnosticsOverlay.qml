import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Diagnostics overlay (3t-2, 3x-2; fullscreen 3y): 340 wide, two collapsible sections. Emulation of the local game (core,
// renderer, resolution, fps, frame time with sparkline, audio, fallback hint) and Streaming per participant. In the
// Multiview (`multi`) Emulation shows your tile only and Streaming one block per tile Session, with the tile badges.
// Fullscreen shows Emulation only (3y). Opened and closed by one toggle (header button, F3); section state is global
// (ADR 0014 D5, kept by the model). Positioned by the game screen: top right of the play area, 16 px from the edges.
Rectangle {
    id: root
    required property DiagnosticsModel model
    property string hotkey: ""
    property bool fullscreen: false
    property bool multi: false
    property var tileBadges: ({})
    readonly property var emu: model.emulation
    readonly property var localTile: {
        var t = model.tiles
        for (var i = 0; i < t.length; ++i) if (t[i].local === true) return t[i]
        return null
    }
    objectName: "diagnosticsOverlay"

    width: 340
    implicitHeight: col.implicitHeight + 28
    radius: 10
    color: Theme.gameOverlayBg
    border.width: 1
    border.color: Theme.gameDivider

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12

        DiagSection {
            objectName: "diagEmulationSection"
            Layout.fillWidth: true
            title: qsTr("EMULATION")
            summary: root.multi && root.localTile ? root.model.tilesSummary : root.model.emulationSummary
            expanded: root.model.emulationOpen
            onToggled: root.model.toggleEmulation()

            FbLabel {
                objectName: "diagNoGame"
                visible: root.emu.valid !== true
                text: qsTr("No game running")
                font.pixelSize: 12
                color: Theme.gameFaint
            }
            // Fallback hint: OpenGL requested but software runs (D10)
            Rectangle {
                objectName: "diagFallbackHint"
                Layout.fillWidth: true
                visible: root.emu.fallback === true && !root.multi
                implicitHeight: hintRow.implicitHeight + 18
                radius: 8
                color: Theme.infoBg
                border.width: 1
                border.color: Theme.infoBorder
                RowLayout {
                    id: hintRow
                    anchors.fill: parent
                    anchors.margins: 9
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8
                    FbLabel { text: "ⓘ"; font.pixelSize: 12; color: Theme.gameAccent; Layout.alignment: Qt.AlignTop }
                    FbLabel {
                        objectName: "diagFallbackText"
                        Layout.fillWidth: true
                        text: root.emu.fallbackText || ""
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: Theme.infoText
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.emu.valid === true && !root.multi
                spacing: 8
                DiagRow { Layout.fillWidth: true; label: qsTr("Core"); value: root.emu.core || ""; valueName: "diagCore" }
                DiagRow {
                    Layout.fillWidth: true
                    label: qsTr("Renderer")
                    value: root.emu.renderer || ""
                    sub: root.emu.rendererSub || ""
                    pill: root.emu.fallback === true ? qsTr("Fallback") : ""
                    valueName: "diagRenderer"
                    subName: "diagRendererSub"
                }
                DiagRow { Layout.fillWidth: true; label: qsTr("Resolution"); value: root.emu.resolution || ""; sub: root.emu.resolutionSub || ""; valueName: "diagResolution"; subName: "diagResolutionSub" }
                DiagRow { Layout.fillWidth: true; label: qsTr("FPS"); value: root.emu.fps || ""; valueName: "diagFps" }
                DiagRow { Layout.fillWidth: true; label: qsTr("Frame"); value: root.emu.frame || ""; valueName: "diagFrame" }
                Sparkline {
                    objectName: "diagSparkline"
                    Layout.leftMargin: 86
                    Layout.preferredWidth: 236
                    Layout.preferredHeight: 48
                    total: root.emu.spark ? root.emu.spark.total : []
                    emu: root.emu.spark ? root.emu.spark.emu : []
                    targetMs: root.emu.spark ? root.emu.spark.targetMs : 16.7
                }
                DiagRow { Layout.fillWidth: true; label: qsTr("Audio"); value: root.emu.audio || ""; sub: root.emu.audioSub || ""; valueName: "diagAudio"; subName: "diagAudioSub" }
            }
            // Multiview: your tile only
            ColumnLayout {
                objectName: "diagTileBlock"
                Layout.fillWidth: true
                visible: root.multi && root.localTile !== null
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle {
                        Layout.preferredWidth: 20; Layout.preferredHeight: 20; radius: 4
                        color: Theme.gameBadge
                        FbMono { anchors.centerIn: parent; text: String(root.localTile ? root.localTile.index : ""); font.pixelSize: 11; color: Theme.gameBadgeText }
                    }
                    FbLabel { Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight; text: root.localTile ? root.localTile.title : ""; font.pixelSize: 13; font.weight: Font.Medium; color: Theme.gameText }
                }
                FbMono {
                    objectName: "diagTileLine1"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    color: Theme.gameBadgeText
                    visible: root.localTile !== null && root.localTile.emulation.valid === true
                    text: root.localTile && root.localTile.emulation.lines ? root.localTile.emulation.lines[2] + " · " + root.localTile.emulation.lines[3] : ""
                }
                FbMono {
                    objectName: "diagTileLine2"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    color: Theme.gameMeta
                    visible: root.localTile !== null && root.localTile.emulation.valid === true
                    text: root.localTile && root.localTile.emulation.lines ? root.localTile.emulation.lines[1] + " · " + root.localTile.emulation.lines[4] : ""
                }
            }
            FbLabel {
                objectName: "diagOnlyYourTile"
                Layout.fillWidth: true
                visible: root.multi
                wrapMode: Text.WordWrap
                text: qsTr("Only your tile is emulated on this device.")
                font.pixelSize: 11
                color: Theme.gameFaint
            }
        }

        // Streaming is not part of the fullscreen overlay (3y)
        Rectangle { visible: !root.fullscreen; Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.gameBorder }
        DiagSection {
            objectName: "diagStreamingSection"
            visible: !root.fullscreen
            Layout.fillWidth: true
            title: qsTr("STREAMING")
            summary: root.model.streamingSummary
            expanded: root.model.streamingOpen
            onToggled: root.model.toggleStreaming()
            DiagParticipants { Layout.fillWidth: true; groups: root.model.streaming; grouped: root.multi; badges: root.tileBadges }
        }

        DiagFooter { Layout.fillWidth: true; hotkey: root.hotkey; onHideRequested: root.model.open = false }
    }
}
