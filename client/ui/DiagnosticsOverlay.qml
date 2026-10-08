import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Diagnostics overlay (3t-3w, 3y): top left over the play area, 350 wide, two collapsible sections. Emulation of the local
// game (core, renderer, resolution, fps, frame time with sparkline, audio, fallback hint) and Streaming per participant. In
// fullscreen only Emulation is shown (3y). Open/closed per section is kept by the model (settings/player.json).
Rectangle {
    id: root
    required property DiagnosticsModel model
    property string hotkey: ""
    property bool fullscreen: false
    readonly property var emu: model.emulation
    objectName: "diagnosticsOverlay"

    width: 350
    implicitHeight: col.implicitHeight + 28
    radius: 10
    color: Theme.overlayBg
    border.width: 1
    border.color: Theme.borderCard

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12

        DiagSection {
            objectName: "diagEmulationSection"
            Layout.fillWidth: true
            title: qsTr("DIAGNOSTICS · EMULATION")
            summary: root.model.emulationSummary
            expanded: root.model.emulationOpen
            onToggled: root.model.toggleEmulation()

            FbLabel {
                objectName: "diagNoGame"
                visible: root.emu.valid !== true
                text: qsTr("No game running")
                font.pixelSize: 12
                color: Theme.textTile
            }
            // Fallback hint: OpenGL requested but software runs (D10)
            Rectangle {
                objectName: "diagFallbackHint"
                Layout.fillWidth: true
                visible: root.emu.fallback === true
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
                    FbLabel { text: "ⓘ"; font.pixelSize: 12; color: Theme.accent; Layout.alignment: Qt.AlignTop }
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
                visible: root.emu.valid === true
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
        }

        // Streaming is not part of the fullscreen overlay (3y)
        Rectangle { visible: !root.fullscreen; Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.gameBorder }
        DiagSection {
            objectName: "diagStreamingSection"
            visible: !root.fullscreen
            Layout.fillWidth: true
            title: qsTr("DIAGNOSTICS · STREAMING")
            summary: root.model.streamingSummary
            expanded: root.model.streamingOpen
            onToggled: root.model.toggleStreaming()
            DiagParticipants { Layout.fillWidth: true; groups: root.model.streaming }
        }

        DiagFooter { Layout.fillWidth: true; hotkey: root.hotkey; onHideRequested: root.model.open = false }
    }
}
