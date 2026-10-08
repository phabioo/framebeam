import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Footer of the diagnostics overlay: key badge (configured diagnostics key) + "show / hide" on the left, "Hide" on the right.
ColumnLayout {
    id: root
    property string hotkey: ""  // configured key label of the diagnostics hotkey; empty = unassigned (no badge)
    signal hideRequested()
    spacing: 0

    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.gameBorder }
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 10
        spacing: 8
        Rectangle {
            visible: root.hotkey !== ""
            implicitWidth: keyLabel.implicitWidth + 12
            implicitHeight: 16
            radius: 3
            color: "transparent"
            border.width: 1
            border.color: Theme.borderButton
            FbMono { id: keyLabel; anchors.centerIn: parent; text: root.hotkey; font.pixelSize: 10; color: Theme.textMuted }
        }
        FbLabel { text: qsTr("show / hide"); font.pixelSize: 11; color: Theme.textFaint }
        Item { Layout.fillWidth: true }
        Text {
            objectName: "diagHide"
            text: qsTr("Hide")
            font.pixelSize: 12
            color: Theme.textMuted
            TapHandler { onTapped: root.hideRequested() }
        }
    }
}
