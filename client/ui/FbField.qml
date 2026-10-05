import QtQuick
import QtQuick.Controls

TextField {
    id: control
    implicitHeight: 44
    leftPadding: 14
    rightPadding: 14
    font.family: Theme.mono
    font.pixelSize: 13
    color: Theme.text
    placeholderTextColor: Theme.textFaint
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    verticalAlignment: TextInput.AlignVCenter
    background: Rectangle {
        radius: 7
        color: Theme.surface
        border.width: control.activeFocus ? 1.5 : 1
        border.color: control.activeFocus ? Theme.accent : Theme.borderInput
    }
}
