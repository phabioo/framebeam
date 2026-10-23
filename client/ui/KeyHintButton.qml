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

    implicitHeight: Theme.row36
    property real maxWidth: 100000   // the text elides so the button is never wider than this (a card narrower than the label)
    implicitWidth: compact ? Theme.row36 : Math.min(maxWidth, row.implicitWidth + 2 * Theme.space15)
    padding: 0
    hoverEnabled: true
    font.family: Theme.sans
    font.pixelSize: Theme.roles.button.size
    font.weight: Theme.roles.button.weight

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
            spacing: Theme.space1
            Text {
                // Elides when maxWidth is smaller than the text (wide fonts).
                Layout.preferredWidth: Math.min(implicitWidth, Math.max(0, control.maxWidth - 2 * Theme.space15 - (hintText.visible ? hintText.implicitWidth + row.spacing : 0)))
                elide: Text.ElideRight
                text: control.text
                textFormat: Text.PlainText
                font: control.font
                color: !control.enabled ? Theme.disabledInk : (control.kind === "primary" ? Theme.inkOnAccent : Theme.ink)
            }
            Text {
                id: hintText
                visible: control.hint !== "" && !control.compact
                text: control.hint
                textFormat: Text.PlainText
                font.family: Theme.mono
                font.pixelSize: Theme.roles.meta11.size
                color: !control.enabled ? Theme.disabledInk : (control.kind === "primary" ? Theme.inkOnAccent : Theme.muted)
            }
        }
    }
    Rectangle {  // compact icon: a square (fullscreen glyph)
        parent: control.contentItem
        visible: control.compact
        anchors.centerIn: parent
        width: 14
        height: 14
        color: "transparent"
        border.width: 2
        border.color: !control.enabled ? Theme.disabledInk : Theme.ink
    }
    background: Item {
        Rectangle {
            anchors.fill: parent
            color: control.kind === "primary" ? (control.enabled ? (control.down ? Theme.accentPressed : (control.hovered ? Theme.accentHover : Theme.accent)) : Theme.disabledGround)
                 : control.kind === "raised" ? Theme.hover
                 : (control.hovered && control.enabled ? Theme.hover : "transparent")
            border.width: 1
            border.color: control.kind === "primary" && control.enabled ? Theme.accent : (control.enabled ? Theme.rule : Theme.disabledRule)
        }
        UzFocusRing { visible: control.visualFocus }
    }
}
