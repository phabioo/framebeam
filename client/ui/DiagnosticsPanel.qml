import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Diagnostics (3h): one mono row per surface; "n/a" where a value is unavailable.
ColumnLayout {
    id: root
    required property PlayerController player
    property bool framed: true   // bottom panel of the multiview (surface + top border)
    readonly property var rows: player.sessions.diagnosticRows
    objectName: "diagnosticsPanel"
    spacing: 8

    RowLayout {
        Layout.fillWidth: true
        FbLabel { text: qsTr("▾ Diagnostics"); font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.gameText }
        FbLabel { text: qsTr("technical details · optional"); font.pixelSize: 12; color: Theme.gameTextMuted }
    }
    FbLabel {
        visible: root.rows.length === 0
        text: qsTr("Nothing to show yet.")
        font.pixelSize: 12
        color: Theme.gameTextMuted
    }
    Repeater {
        model: root.rows
        delegate: FbMono {
            required property var modelData
            objectName: "diagRow_" + modelData.surface
            Layout.fillWidth: true
            text: modelData.text
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: Theme.gameText
        }
    }
}
