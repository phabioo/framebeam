import QtQuick
import QtQuick.Controls

// kind: "primary" (Akzent) | "outline" | "link"
Button {
    id: control
    property string kind: "outline"

    implicitHeight: kind === "link" ? 28 : 40
    implicitWidth: Math.max(kind === "link" ? 0 : 88, labelItem.implicitWidth + (kind === "link" ? 8 : 32))
    padding: 0
    hoverEnabled: true
    font.pixelSize: 14
    font.weight: kind === "primary" ? Font.DemiBold : Font.Medium

    contentItem: Text {
        id: labelItem
        text: control.text
        font: control.font
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: !control.enabled ? Theme.textDisabled
               : control.kind === "primary" ? Theme.textOnAccent
               : control.kind === "link" ? (control.hovered ? Theme.text : Theme.textMuted)
               : Theme.text
    }
    background: Rectangle {
        visible: control.kind !== "link"
        radius: 8
        color: control.kind === "primary"
               ? (control.enabled ? (control.down ? Qt.darker(Theme.accent, 1.15) : (control.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent)) : Theme.surfaceRaised)
               : (control.hovered && control.enabled ? Theme.surfaceRaised : "transparent")
        border.width: control.kind === "primary" ? 0 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.borderButton
    }
}
