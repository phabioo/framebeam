import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// Button with a key hint in mono ("Fullscreen  F11", "Save snapshot  F5"). kind: "outline" | "primary" | "raised".
Button {
    id: control
    property string hint: ""
    property string kind: "outline"
    // Narrow layouts: a small square icon instead of label and key hint (the shortcut still works; the label is the tooltip).
    property bool compact: false

    implicitHeight: 34
    property real maxWidth: 100000   // the text elides so the button is never wider than this (a card narrower than the label)
    implicitWidth: compact ? 40 : Math.min(maxWidth, row.implicitWidth + 24)
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
                // Elides when maxWidth is smaller than the text (wide fonts).
                Layout.preferredWidth: Math.min(implicitWidth, Math.max(0, control.maxWidth - 24 - (hintText.visible ? hintText.implicitWidth + row.spacing : 0)))
                elide: Text.ElideRight
                text: control.text
                textFormat: Text.PlainText
                font: control.font
                color: !control.enabled ? Theme.textDisabled : (control.kind === "primary" ? Theme.textOnAccent : Theme.text)
            }
            Text {
                id: hintText
                visible: control.hint !== "" && !control.compact
                text: control.hint
                textFormat: Text.PlainText
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
        radius: 0
        color: "transparent"
        border.width: 2
        border.color: !control.enabled ? Theme.textDisabled : Theme.text
    }
    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: 0
            color: control.kind === "primary" ? (control.enabled ? (control.down ? Theme.accentPressed : (control.hovered ? Theme.accentHover : Theme.accent)) : Theme.disabledGround)
                 : control.kind === "raised" ? Theme.hover
                 : (control.hovered && control.enabled ? Theme.hover : "transparent")
            border.width: 1
            border.color: control.kind === "primary" && control.enabled ? Theme.accent : (control.enabled ? Theme.rule : Theme.disabledRule)
        }
        UzFocusRing { visible: control.visualFocus }
    }
}
