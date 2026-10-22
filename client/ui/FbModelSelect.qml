import QtQuick
import QtQuick.Controls.Basic
import Uzume.App

// Select field (34 high) over a C++ list model with the roles pickValue / pickLabel (large lists: the rows are created
// lazily, nothing is converted to JS up front). current = selected value; picked(value) on user choice. Same look as FbSelect.
ComboBox {
    id: control
    property string current: ""
    signal picked(string value)

    implicitHeight: 34
    implicitWidth: 200
    textRole: "pickLabel"
    valueRole: "pickValue"
    currentIndex: control.indexOfValue(control.current)
    onActivated: index => control.picked(control.valueAt(index))

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
        required property string pickValue
        required property string pickLabel
        required property int index
        width: control.width
        height: 32
        padding: 0
        leftPadding: 12
        contentItem: Text {
            text: item.pickLabel
            font: control.font
            color: item.pickValue === control.current ? Theme.accent : Theme.text
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
