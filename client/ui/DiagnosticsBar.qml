import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Diagnostics panel at the bottom of the multiview (3h, 3x, windowed only): the same two sections, laid out across the
// width. Emulation per tile (local: values in one wrapped mono text and a small sparkline; remote: "runs on {who}'s Player"),
// Streaming grouped per tile Session.
Rectangle {
    id: root
    required property DiagnosticsModel model
    property string hotkey: ""
    objectName: "diagnosticsPanel"
    implicitHeight: col.implicitHeight + 26
    color: Theme.gameHeader

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.gameBorder }

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.topMargin: 12
        anchors.bottomMargin: 14
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        spacing: 10

        DiagSection {
            objectName: "diagEmulationSection"
            Layout.fillWidth: true
            indent: 18
            title: qsTr("DIAGNOSTICS · EMULATION")
            summary: root.model.tilesSummary
            expanded: root.model.emulationOpen
            onToggled: root.model.toggleEmulation()

            Flow {
                Layout.fillWidth: true
                spacing: 24
                Repeater {
                    model: root.model.tiles
                    delegate: ColumnLayout {
                        id: tile
                        required property var modelData
                        objectName: "diagTile_" + modelData.surface
                        width: 320
                        spacing: 3
                        FbLabel { text: tile.modelData.title; font.pixelSize: 13; font.weight: Font.Medium; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
                        FbMono { text: tile.modelData.sub; font.pixelSize: 11; color: Theme.textMeta }
                        FbMono {
                            objectName: "diagTileLines"
                            Layout.fillWidth: true
                            visible: tile.modelData.local === true && tile.modelData.emulation.valid === true
                            text: tile.modelData.emulation.lines ? tile.modelData.emulation.lines.join(" · ") : ""
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: Theme.text
                        }
                        Sparkline {
                            visible: tile.modelData.local === true && tile.modelData.emulation.valid === true
                            legend: false
                            Layout.preferredWidth: 200
                            Layout.preferredHeight: 22
                            total: tile.modelData.emulation.spark ? tile.modelData.emulation.spark.total : []
                            emu: tile.modelData.emulation.spark ? tile.modelData.emulation.spark.emu : []
                            targetMs: tile.modelData.emulation.spark ? tile.modelData.emulation.spark.targetMs : 16.7
                        }
                        FbLabel {
                            objectName: "diagTileNote"
                            Layout.fillWidth: true
                            visible: tile.modelData.local !== true
                            text: tile.modelData.note
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: Theme.textTile
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.gameBorder }

        DiagSection {
            objectName: "diagStreamingSection"
            Layout.fillWidth: true
            indent: 18
            title: qsTr("DIAGNOSTICS · STREAMING")
            summary: root.model.participantCount > 0 ? qsTr("%1 participants · %2").arg(root.model.participantCount).arg(root.model.streamingSummary.split(" · ").slice(1).join(" · "))
                                                     : root.model.streamingSummary
            expanded: root.model.streamingOpen
            onToggled: root.model.toggleStreaming()

            Flow {
                Layout.fillWidth: true
                spacing: 24
                Repeater {
                    model: root.model.streaming
                    delegate: DiagParticipants {
                        required property var modelData
                        width: 320
                        grouped: true
                        groups: [modelData]
                    }
                }
                FbLabel {
                    objectName: "diagStreamingEmpty"
                    visible: root.model.streaming.length === 0
                    text: qsTr("No active session · appears when you share or watch one")
                    font.pixelSize: 12
                    color: Theme.textTile
                }
            }
        }

        DiagFooter { Layout.fillWidth: true; hotkey: root.hotkey; onHideRequested: root.model.open = false }
    }
}
