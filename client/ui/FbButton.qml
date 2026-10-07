import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// kind: "primary" (accent) | "outline" | "link".
// Feedback: hover and pressed states change at once (short color transition); `busy` shows a spinner and blocks
// repeated clicks while network work runs; `busyOnClick` shows it right on the click until the state arrives
// (or after at most 1.5 s), so a button that starts network work never looks dead.
Button {
    id: control
    property string kind: "outline"
    property bool busy: false
    property bool busyOnClick: false
    readonly property bool working: busy || clickBusy
    property bool clickBusy: false

    implicitHeight: kind === "link" ? 28 : 40
    implicitWidth: Math.max(kind === "link" ? 0 : 88, labelRow.implicitWidth + (kind === "link" ? 8 : 32))
    padding: 0
    Layout.minimumWidth: kind === "link" ? 0 : implicitWidth
    hoverEnabled: true
    font.pixelSize: Theme.fontBody
    font.weight: kind === "primary" ? Font.DemiBold : Font.Medium
    opacity: 1

    onClicked: if (busyOnClick) { clickBusy = true; busyReset.restart() }
    onBusyChanged: if (!busy) clickBusy = false
    onVisibleChanged: if (!visible) clickBusy = false
    // While work runs, further clicks and key activations are swallowed (the look stays enabled).
    MouseArea { anchors.fill: parent; visible: control.working; z: 10; acceptedButtons: Qt.AllButtons; cursorShape: Qt.BusyCursor }
    Keys.onPressed: (event) => { if (control.working && (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) event.accepted = true }
    Keys.onReleased: (event) => { if (control.working && (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) event.accepted = true }
    Timer { id: busyReset; interval: 1500; onTriggered: control.clickBusy = false }

    contentItem: Item {
        implicitWidth: labelRow.implicitWidth
        implicitHeight: labelRow.implicitHeight
        Row {
            id: labelRow
            anchors.centerIn: parent
            spacing: 8
            FbSpinner {
                visible: control.working
                size: 12
                color: control.kind === "primary" ? Theme.textOnAccent : Theme.text
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                id: labelItem
                text: control.text
                font: control.font
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                anchors.verticalCenter: parent.verticalCenter
                color: !control.enabled ? Theme.textDisabled
                       : control.kind === "primary" ? Theme.textOnAccent
                       : control.kind === "link" ? (control.hovered ? Theme.text : Theme.textMuted)
                       : Theme.text
                Behavior on color { ColorAnimation { duration: Theme.durFast } }
            }
        }
    }
    background: Rectangle {
        visible: control.kind !== "link"
        radius: control.kind === "primary" ? Theme.radius8 : Theme.radius7
        color: control.kind === "primary"
               ? (control.enabled ? (control.down ? Qt.darker(Theme.accent, 1.18) : (control.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)) : Theme.borderInput)
               : (control.enabled && control.down ? Theme.borderInput : (control.hovered && control.enabled ? Theme.surfaceRaised : "transparent"))
        border.width: control.kind === "primary" ? 0 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.borderButton
        scale: control.down && control.enabled ? 0.985 : 1
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
        Behavior on scale { NumberAnimation { duration: 80 } }
    }
}
