import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Button with a key hint in mono ("Fullscreen  F11", "Save snapshot  F5"). kind: "outline" | "primary" | "raised".
Button {
    id: control
    property string hint: ""
    property string kind: "outline"

    implicitHeight: 34
    implicitWidth: row.implicitWidth + 24
    padding: 0
    hoverEnabled: true
    font.pixelSize: 13
    font.weight: Font.Medium

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Text {
                text: control.text
                font: control.font
                color: !control.enabled ? Theme.textDisabled : (control.kind === "primary" ? Theme.textOnAccent : Theme.text)
            }
            Text {
                visible: control.hint !== ""
                text: control.hint
                font.family: Theme.mono
                font.pixelSize: 11
                color: !control.enabled ? Theme.textDisabled : (control.kind === "primary" ? Theme.textOnAccent : Theme.textMuted)
            }
        }
    }
    background: Rectangle {
        radius: 7
        color: control.kind === "primary" ? (control.enabled ? Theme.accent : Theme.surfaceRaised)
             : control.kind === "raised" ? Theme.surfaceRaised
             : (control.hovered && control.enabled ? Theme.surfaceRaised : "transparent")
        border.width: control.kind === "primary" ? 0 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.borderButton
    }
}
