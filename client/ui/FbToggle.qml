import QtQuick
import QtQuick.Controls

Switch {
    id: control
    implicitHeight: 28
    padding: 0
    spacing: 12

    indicator: Rectangle {
        implicitWidth: 40
        implicitHeight: 22
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 11
        color: control.checked ? Theme.accent : Theme.surfaceRaised
        border.width: 1
        border.color: control.visualFocus ? Theme.accent : (control.checked ? Theme.accent : Theme.borderButton)
        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            y: 3
            width: 16
            height: 16
            radius: 8
            color: control.checked ? Theme.textOnAccent : Theme.textMuted
        }
    }
    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font.pixelSize: 13
        color: Theme.textSecondary
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
}
