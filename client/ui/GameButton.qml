import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Button of the in-game header and panel (docs/design/player.md GameHeader "Button style"): 30 high, radius 7, padding
// 0 10, 13/500, transparent inside a group box; hover #1f2024; on state (Speed-up, Diagnostics, open popover) accent on
// #15262b; danger (Reset popover open) #ef8a78 on #2a1a17. look: "flat" | "outlined" (border #3a3b40, 32 high) |
// "surface" (own surface and border, 32 high). Always dark (the game view is dark in both themes).
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

    readonly property color fg: !enabled ? Theme.gameFaint : on ? Theme.gameAccent : danger ? Theme.gameDanger : muted ? Theme.gameTextMuted : Theme.gameText

    implicitHeight: look === "flat" ? 30 : 32
    implicitWidth: fixedWidth > 0 ? fixedWidth : row.implicitWidth + 2 * hPad + (look === "outlined" ? 2 : 0)
    Layout.minimumWidth: implicitWidth
    Layout.alignment: Qt.AlignVCenter
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    font.pixelSize: 13
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
                    font.pixelSize: control.monoLabel ? 12 : control.font.pixelSize
                    font.weight: control.font.weight
                    font.family: control.monoLabel ? Theme.mono : Qt.application.font.family
                    color: control.fg
                }
                Text { id: probe; visible: false; text: control.widthText; font: label.font }
            }
            Text {
                visible: control.meta !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: control.meta
                font.family: Theme.mono
                font.pixelSize: 11
                color: control.on ? Theme.gameAccent : Theme.gameFaint
            }
            Rectangle {
                visible: control.hint !== ""
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: keyText.implicitWidth + 8
                implicitHeight: keyText.implicitHeight + 2
                radius: 3
                color: "transparent"
                border.width: 1
                border.color: control.on ? Theme.gameOnBorder : Theme.gameKeyLine
                Text {
                    id: keyText
                    anchors.centerIn: parent
                    text: control.hint
                    font.family: Theme.mono
                    font.pixelSize: 10
                    font.weight: Font.Medium
                    color: control.on ? Theme.gameAccent : Theme.gameTextMuted
                }
            }
            GameIcon {
                objectName: "caretIcon"
                visible: control.caret !== ""
                anchors.verticalCenter: parent.verticalCenter
                kind: "caret"
                color: Theme.gameFaint
            }
        }
    }
    background: Rectangle {
        visible: !control.plain
        radius: 7
        color: control.on ? (control.look === "surface" ? Theme.gameOnBg : Theme.gameOnBg)
             : control.danger ? Theme.gameDangerBg
             : control.look === "surface" ? (control.hovered ? Theme.gameHover : Theme.gameTrack)
             : (control.hovered && control.enabled ? Theme.gameHover : "transparent")
        border.width: control.look === "flat" ? 0 : 1
        border.color: control.on && control.look === "surface" ? Theme.gameAccent
                    : control.look === "outlined" ? Theme.gameKeyLine : Theme.gameDivider
    }
}
