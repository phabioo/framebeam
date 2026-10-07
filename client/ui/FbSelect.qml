import QtQuick
import QtQuick.Controls.Basic
import FrameBeam.Player

// Select field (34 high): options [{ value, label }], current = selected value; picked(value) on user choice.
ComboBox {
    id: control
    property string current: ""
    signal picked(string value)

    implicitHeight: 34
    implicitWidth: 200
    textRole: "label"
    valueRole: "value"
    currentIndex: {
        for (let i = 0; i < control.count; ++i) {
            if (control.model[i] && control.model[i].value === control.current) {
                return i
            }
        }
        return -1
    }
    onActivated: index => control.picked(control.model[index].value)

    font.pixelSize: Theme.fontSmall
    leftPadding: 12
    rightPadding: 28

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    indicator: Text {
        x: control.width - width - 12
        y: (control.height - height) / 2
        text: "▾"
        font.pixelSize: Theme.fontMeta
        color: Theme.textMuted
    }
    background: Rectangle {
        radius: Theme.radius7
        color: Theme.surface
        border.width: control.visualFocus || control.popup.visible ? 1.5 : 1
        border.color: control.visualFocus || control.popup.visible ? Theme.accent : (control.hovered ? Theme.borderButton : Theme.borderInput)
        Behavior on border.color { ColorAnimation { duration: Theme.durFast } }
    }
    delegate: ItemDelegate {
        id: item
        required property var modelData
        required property int index
        width: control.width
        height: 32
        padding: 0
        leftPadding: 12
        contentItem: Text {
            text: item.modelData.label
            font: control.font
            color: item.modelData.value === control.current ? Theme.accent : Theme.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { color: item.highlighted || item.hovered ? Theme.surfaceRaised : "transparent" }
        highlighted: control.highlightedIndex === item.index
    }
    popup: Popup {
        y: control.height + 2
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 320)
        padding: 1
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }
        background: Rectangle {
            radius: Theme.radius7
            color: Theme.popupBg
            border.width: 1
            border.color: Theme.borderPopup
        }
    }
}
