import QtQuick
import QtQuick.Controls.Basic

// Menu popup (design-system.md "Menu"): a UzMenuList in a Popup that opens below (or above) an anchor item, closes on Esc,
// on a click outside and after a choice. items: see UzMenuList. `chosen(item, index)` is emitted before it closes.
//   UzMenu { id: menu; items: [{ label: qsTr("Rename"), shortcut: "F2" }, { separator: true }, { label: qsTr("Delete"), danger: true }] }
//   menu.openFor(button)              // below, left edge aligned; openFor(button, "above", true) opens upward, right aligned
Popup {
    id: root
    property var items: []
    property bool selectable: false
    property var value: undefined
    property int menuWidth: Theme.menu
    readonly property alias list: list
    signal chosen(var item, int index)

    function openFor(anchor: Item, placement: string, alignRight: bool) {
        parent = anchor
        const gap = Theme.space05
        x = alignRight === true ? anchor.width - width : 0
        y = placement === "above" ? -height - gap : anchor.height + gap
        open()
    }

    padding: 0
    margins: Theme.space1
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.max(Theme.menu, menuWidth)
    onAboutToShow: list.activeIndex = -1
    onOpened: list.forceActiveFocus()
    background: Item {}
    contentItem: UzMenuList {
        id: list
        items: root.items
        selectable: root.selectable
        value: root.value
        onChosen: (item, index) => { root.chosen(item, index); root.close() }
    }
}
