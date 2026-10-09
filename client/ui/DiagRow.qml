import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// Label (76) + mono value + optional subline and pill (3t-3y "Emulation rows").
RowLayout {
    id: root
    property string label: ""
    property string value: ""
    property string sub: ""
    property string pill: ""          // e.g. "Fallback"
    property string pillTone: "warn"
    property string valueName: ""     // objectName of the value text (tests)
    property string subName: ""
    spacing: 10

    FbLabel {
        Layout.preferredWidth: 76
        Layout.alignment: Qt.AlignTop
        text: root.label
        font.pixelSize: 11
        color: Theme.gameFaint
    }
    ColumnLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            FbMono {
                objectName: root.valueName
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.value
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: Theme.gameText
            }
            FbPill {
                objectName: root.valueName !== "" ? root.valueName + "Pill" : ""
                visible: root.pill !== ""
                small: true
                text: root.pill
                tone: root.pillTone
            }
        }
        FbMono {
            objectName: root.subName
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: root.sub !== ""
            text: root.sub
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: Theme.gameMeta
        }
    }
}
