import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Streaming body of the diagnostics overlay (3v, 3x): per participant name, role, connection pill, encoder or decoder line and
// the link line. groups = DiagnosticsModel.streaming; grouped shows the Session title above each group (multiview).
ColumnLayout {
    id: root
    property var groups: []
    property bool grouped: false
    property var badges: ({})         // surface id -> tile number: a key badge before each group (Multiview)
    spacing: 10

    FbLabel {
        objectName: "diagStreamingEmpty"
        Layout.fillWidth: true
        visible: root.groups.length === 0
        text: qsTr("No active session · appears when you share or watch one")
        wrapMode: Text.WordWrap
        font.pixelSize: 12
        color: Theme.gameFaint
    }
    Repeater {
        model: root.groups
        delegate: ColumnLayout {
            id: grp
            required property var modelData
            Layout.fillWidth: true
            spacing: 8
            RowLayout {
                visible: root.grouped
                spacing: 8
                Rectangle {
                    objectName: "diagGroupBadge"
                    visible: root.badges[grp.modelData.surface] !== undefined
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    radius: 4
                    color: Theme.gameBadge
                    FbMono { anchors.centerIn: parent; text: String(root.badges[grp.modelData.surface]); font.pixelSize: 11; color: Theme.gameBadgeText }
                }
                FbMono {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                    text: grp.modelData.title
                    font.pixelSize: 11
                    color: Theme.gameFaint
                }
            }
            Repeater {
                model: grp.modelData.participants
                delegate: ColumnLayout {
                    id: person
                    required property var modelData
                    objectName: "diagParticipant"
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        FbLabel { text: person.modelData.name; font.pixelSize: 13; font.weight: Font.Medium; color: Theme.gameText }
                        FbLabel { text: person.modelData.role; font.pixelSize: 11; color: Theme.gameFaint }
                        Item { Layout.fillWidth: true }
                        FbPill { objectName: "diagParticipantPill"; small: true; text: person.modelData.pill; tone: person.modelData.tone }
                    }
                    FbMono {
                        objectName: "diagParticipantLine2"
                        Layout.fillWidth: true
                        text: person.modelData.line2
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: Theme.gameBadgeText
                    }
                    FbMono {
                        objectName: "diagParticipantLine3"
                        Layout.fillWidth: true
                        text: person.modelData.line3
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: Theme.gameMeta
                    }
                }
            }
        }
    }
}
