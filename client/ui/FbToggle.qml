import QtQuick
import QtQuick.Controls.Basic

// Toggle (tokens.md): 40 x 22, radius 11, knob 16 at top 3, left 3 (off) / 21 (on). Player: on = accent, off = #2c2d32.
Switch {
    id: control
    implicitHeight: 28
    padding: 0
    spacing: 12
    hoverEnabled: true

    indicator: Rectangle {
        implicitWidth: 40
        implicitHeight: 22
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: Theme.radius11
        color: control.checked ? Theme.accent : Theme.borderInput
        border.width: control.visualFocus ? 1.5 : 0
        border.color: Theme.accent
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
        Rectangle {
            x: control.checked ? 21 : 3
            y: 3
            width: 16
            height: 16
            radius: 8
            color: control.checked ? Theme.textOnAccent : Theme.text
            Behavior on x { NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font.pixelSize: Theme.fontSmall
        color: Theme.textSecondary
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
}
