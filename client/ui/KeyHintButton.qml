import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Button with a key hint in mono ("Fullscreen  F11", "Save snapshot  F5"). kind: "outline" | "primary" | "raised".
Button {
    id: control
    property string hint: ""
    property string kind: "outline"
    // Narrow layouts: a small square icon instead of label and key hint (the shortcut still works; the label is the tooltip).
    property bool compact: false

    implicitHeight: 34
    implicitWidth: compact ? 40 : row.implicitWidth + 24
    padding: 0
    hoverEnabled: true
    font.pixelSize: 13
    font.weight: Font.Medium

    Accessible.name: control.text
    ToolTip.visible: compact && hovered
    ToolTip.text: control.hint !== "" ? control.text + " (" + control.hint + ")" : control.text

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        RowLayout {
            id: row
            visible: !control.compact
            anchors.centerIn: parent
            spacing: 8
            Text {
                text: control.text
                font: control.font
                color: !control.enabled ? Theme.textDisabled : (control.kind === "primary" ? Theme.textOnAccent : Theme.text)
            }
            Text {
                visible: control.hint !== "" && !control.compact
                text: control.hint
                font.family: Theme.mono
                font.pixelSize: 11
                color: !control.enabled ? Theme.textDisabled : (control.kind === "primary" ? Theme.textOnAccent : Theme.textMuted)
            }
        }
    }
    Rectangle {  // compact icon: a square (fullscreen glyph)
        parent: control.contentItem
        visible: control.compact
        anchors.centerIn: parent
        width: 14
        height: 14
        radius: 2
        color: "transparent"
        border.width: 2
        border.color: !control.enabled ? Theme.textDisabled : Theme.text
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
