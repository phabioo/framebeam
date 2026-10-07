import QtQuick
import QtQuick.Controls.Basic

// Text input (mono). `invalid` draws the error line (Theme.inputError); focus draws the accent ring.
TextField {
    id: control
    property bool invalid: false
    implicitHeight: 44
    leftPadding: 14
    rightPadding: 14
    font.family: Theme.mono
    font.pixelSize: Theme.fontSmall
    color: Theme.text
    placeholderTextColor: Theme.textFaint
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    verticalAlignment: TextInput.AlignVCenter
    background: Rectangle {
        radius: Theme.radius7
        color: Theme.surface
        border.width: control.invalid || control.activeFocus ? 1.5 : 1
        border.color: control.invalid ? Theme.inputError : (control.activeFocus ? Theme.accent : Theme.borderInput)
        Behavior on border.color { ColorAnimation { duration: Theme.durFast } }
    }
}
