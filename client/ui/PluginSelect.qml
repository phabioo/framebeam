import QtQuick
import QtQuick.Controls.Basic

// select block: options { value, label }; selected = value. Emits action(action, value).
ComboBox {
    id: control
    property var node: ({})
    property bool dark: true
    property string pluginId: ""
    signal action(string action, var value)

    model: node.options || []
    textRole: "label"
    implicitHeight: 34
    implicitWidth: 200
    font.pixelSize: Theme.fontSmall
    leftPadding: 12
    rightPadding: 28
    currentIndex: {
        const o = node.options || []
        for (let i = 0; i < o.length; ++i) {
            if (String(o[i].value) === String(node.selected)) return i
        }
        return -1
    }
    onActivated: (index) => control.action(control.node.action || "", control.model[index].value)

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: Theme.blockColor("text", control.dark)
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    indicator: Text {
        x: control.width - width - 12
        y: (control.height - height) / 2
        text: "▾"
        font.pixelSize: Theme.fontMeta
        color: Theme.blockColor("textMuted", control.dark)
    }
    background: Rectangle {
        radius: Theme.radius7
        color: Theme.blockColor("card", control.dark)
        border.width: control.visualFocus || control.popup.visible ? 2 : 1
        border.color: control.visualFocus || control.popup.visible ? Theme.blockColor("accent", control.dark) : Theme.blockColor("border", control.dark)
    }
    delegate: ItemDelegate {
        id: item
        required property var modelData
        required property int index
        width: control.width
        height: 32
        padding: 0
        leftPadding: 12
        highlighted: control.highlightedIndex === item.index
        contentItem: Text {
            text: item.modelData.label
            font: control.font
            color: item.index === control.currentIndex ? Theme.blockColor("accent", control.dark) : Theme.blockColor("text", control.dark)
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { color: item.highlighted || item.hovered ? Theme.blockColor("hover", control.dark) : "transparent" }
    }
    popup: Popup {
        y: control.height + 2
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 280)
        padding: 1
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
        }
        background: Rectangle {
            radius: Theme.radius7
            color: Theme.blockColor("popup", control.dark)
            border.width: 1
            border.color: Theme.blockColor("popupBorder", control.dark)
        }
    }
}
