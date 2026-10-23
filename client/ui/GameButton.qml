import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// Compact button of the plugin surfaces hosted in the game view (plugin panel, console, screen sheet); the game header and panel
// use UzButton. Square, 30 high, padding 0 10, 13/500, transparent; hover `hover`; `on` (pressed / toggled) = `pressed` plane with an
// `ink-2` frame, danger = danger text on the danger tint. look: "flat" | "outlined" (`rule` frame, 32 high) | "surface" (same,
// 32 high).
Button {
    id: control
    property string iconKind: ""
    property string glyph: ""        // text glyph instead of an icon ("↺", "⋯", "←")
    property string hint: ""         // key badge ("F3")
    property string meta: ""         // mono 11 suffix ("· 3/4")
    property string caret: ""        // trailing "▾"
    property string tip: ""          // tooltip; also the accessible name
    property string widthText: ""    // widest alternative label (keeps the width when the label changes)
    property string look: "flat"
    property bool on: false
    property bool danger: false
    property bool iconOnly: false
    property bool plain: false       // no own background (part of a split button)
    property bool monoLabel: false
    property bool muted: false       // quiet text (Back)
    property int hPad: 10
    property int fixedWidth: 0
    property bool showMeta: true     // false: the mono suffix gives way (header with very wide fonts)
    readonly property real metaFullWidth: meta !== "" ? metaText.implicitWidth + row.spacing : 0
    property bool showHint: true     // false: the key badge gives way (header with very wide fonts)
    readonly property real hintFullWidth: hint !== "" ? keyText.implicitWidth + 8 + row.spacing : 0   // badge incl. gap, whether shown or not

    readonly property color fg: !enabled ? Theme.disabledInk : on ? Theme.ink : danger ? Theme.danger : muted ? Theme.muted : Theme.ink

    implicitHeight: look === "flat" ? 30 : 32
    implicitWidth: fixedWidth > 0 ? fixedWidth : row.implicitWidth + 2 * hPad + (look === "outlined" ? 2 : 0)
    Layout.minimumWidth: implicitWidth
    Layout.alignment: Qt.AlignVCenter
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    font.family: Theme.sans
    font.pixelSize: Theme.roles.buttonSm.size
    font.weight: Font.Medium
    Accessible.name: tip !== "" ? tip : text
    ToolTip.visible: hovered && tip !== ""
    ToolTip.delay: 500
    ToolTip.text: tip

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 7
            GameIcon {
                objectName: "iconItem"
                visible: control.iconKind !== ""
                anchors.verticalCenter: parent.verticalCenter
                kind: control.iconKind
                color: control.fg
            }
            Text {
                visible: control.glyph !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: control.glyph
                font.pixelSize: control.glyph === "↺" ? 14 : 15
                color: control.fg
            }
            Item {
                visible: !control.iconOnly && control.text !== ""
                anchors.verticalCenter: parent.verticalCenter
                width: visible ? Math.max(label.implicitWidth, probe.implicitWidth) : 0
                height: label.implicitHeight
                Text {
                    id: label
                    objectName: "labelText"
                    anchors.centerIn: parent
                    verticalAlignment: Text.AlignVCenter
                    text: control.text
                    textFormat: Text.PlainText
                    font.pixelSize: control.monoLabel ? Theme.roles.small.size : control.font.pixelSize
                    font.weight: control.font.weight
                    font.family: control.monoLabel ? Theme.mono : Qt.application.font.family
                    color: control.fg
                }
                Text { id: probe; visible: false; text: control.widthText; font: label.font }
            }
            Text {
                id: metaText
                visible: control.meta !== "" && control.showMeta
                anchors.verticalCenter: parent.verticalCenter
                text: control.meta
                textFormat: Text.PlainText
                font.family: Theme.mono
                font.pixelSize: 11
                color: control.on ? Theme.ink : Theme.dim
            }
            Rectangle {
                visible: control.hint !== "" && control.showHint
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: keyText.implicitWidth + 8
                implicitHeight: keyText.implicitHeight + 2
                radius: 0
                color: "transparent"
                border.width: 1
                border.color: control.on ? Theme.ink2 : Theme.rule
                Text {
                    id: keyText
                    anchors.centerIn: parent
                    text: control.hint
                    textFormat: Text.PlainText
                    font.family: Theme.mono
                    font.pixelSize: 10
                    font.weight: Font.Medium
                    color: control.on ? Theme.ink : Theme.muted
                }
            }
            GameIcon {
                objectName: "caretIcon"
                visible: control.caret !== ""
                anchors.verticalCenter: parent.verticalCenter
                kind: "caret"
                color: Theme.dim
            }
        }
    }
    background: Rectangle {
        visible: !control.plain
        radius: 0
        color: control.on ? Theme.pressed
             : control.danger ? Theme.dangerTint
             : control.look === "surface" ? (control.hovered ? Theme.hover : "transparent")
             : (control.hovered && control.enabled ? Theme.hover : "transparent")
        border.width: control.look === "flat" ? 0 : 1
        border.color: control.on ? Theme.ink2 : Theme.rule
    }
}
