import QtQuick
import QtQuick.Controls.Basic

// Icon button (design-system.md "IconButton"): 32 px square ghost control for header buttons (volume, bell, close) that may
// anchor a popover. `glyph` is a UzGlyph name; `on` = its popover is open (pressed plane). `count` draws the Yamabuki badge
// (9+ above nine).
Button {
    id: control
    property string glyph: "more"
    property bool on: false
    property int count: 0
    property int glyphSize: 16
    implicitWidth: Theme.row32
    implicitHeight: Theme.row32
    padding: 0
    hoverEnabled: true
    Accessible.name: control.text

    contentItem: Item {
        UzGlyph {
            anchors.centerIn: parent
            name: control.glyph
            size: control.glyphSize
            color: control.enabled ? Theme.ink2 : Theme.disabledInk
        }
        Rectangle {
            visible: control.count > 0
            objectName: "iconButtonCount"
            x: parent.width - width
            y: 2
            width: Math.max(14, countText.implicitWidth + 6)
            height: 14
            radius: 0
            color: Theme.accent
            Text {
                id: countText
                anchors.centerIn: parent
                text: control.count > 9 ? "9+" : String(control.count)
                font.family: Theme.mono
                font.pixelSize: 9
                font.weight: Font.DemiBold
                color: Theme.inkOnAccent
            }
        }
    }
    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: 0
            color: control.down || control.on ? Theme.pressed : (control.hovered && control.enabled ? Theme.hover : "transparent")
        }
        UzFocusRing { visible: control.visualFocus }
    }
}
