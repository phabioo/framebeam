import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Footer of the diagnostics overlay (3t-2): "Hide diagnostics" + key badge (the configured diagnostics key), right-aligned.
// The same label and key as the header button.
ColumnLayout {
    id: root
    property string hotkey: ""  // configured key label of the diagnostics hotkey; empty = unassigned (no badge)
    signal hideRequested()
    spacing: 0

    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.gameBorder }
    RowLayout {
        id: hideRow
        objectName: "diagHide"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.topMargin: 10
        spacing: 8
        Item { Layout.fillWidth: true }
        Text {
            text: qsTr("Hide diagnostics")
            font.pixelSize: 12
            color: Theme.gameTextMuted
        }
        Rectangle {
            visible: root.hotkey !== ""
            implicitWidth: keyLabel.implicitWidth + 8
            implicitHeight: keyLabel.implicitHeight + 2
            radius: 3
            color: "transparent"
            border.width: 1
            border.color: Theme.gameKeyLine
            FbMono { id: keyLabel; anchors.centerIn: parent; text: root.hotkey; font.pixelSize: 10; font.weight: Font.Medium; color: Theme.gameTextMuted }
        }
        TapHandler { onTapped: root.hideRequested() }
    }
}
