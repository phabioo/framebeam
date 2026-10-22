import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// "⋯" button with a menu (design system "Menu": `surface` plane, 1 px `rule` frame, 32 px items, danger item in the danger text,
// no shadow): 30 x 30, the last column of a row. Items are
// [{ name, label, danger?, divider?, enabled? }]; a destructive item is red, comes after a divider and ends with "…".
// `activated(name)` is emitted for the chosen item; the menu items are named "<objectName>_<name>".
Item {
    id: root
    property var items: []
    property string tooltip: qsTr("More")
    property int menuWidth: 220
    readonly property bool opened: popLoader.item !== null && popLoader.item.opened
    signal activated(string name)
    function open() {
        popLoader.active = true
        if (popLoader.item) popLoader.item.open()
    }
    function close() { if (popLoader.item) popLoader.item.close() }

    implicitWidth: 30
    implicitHeight: 30
    Layout.preferredWidth: 30
    Layout.preferredHeight: 30
    Accessible.role: Accessible.Button
    Accessible.name: root.tooltip
    Accessible.onPressAction: root.open()
    // Keyboard (R-P4): Tab reaches the button, Enter/Space/Down opens the menu.
    activeFocusOnTab: true
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space || event.key === Qt.Key_Down) {
            root.open()
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 0
        color: root.opened ? Theme.pressed : (hover.hovered ? Theme.hover : "transparent")
        border.width: 1
        border.color: root.opened || hover.hovered ? Theme.ink2 : Theme.rule
        UzGlyph { anchors.centerIn: parent; name: "more"; size: 16; color: Theme.ink2 }
    }
    UzFocusRing { visible: root.activeFocus }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.opened ? root.close() : root.open() }

    // The popup is created on the first open (a closed menu costs nothing and adds nothing to the layout of its row).
    Loader {
        id: popLoader
        active: false
        sourceComponent: popComp
    }
    Component {
        id: popComp
        Popup {
            id: pop
            y: root.height + 4
            x: root.width - width
            width: root.menuWidth
            padding: 1
            topPadding: 1 + Theme.space05
            bottomPadding: 1 + Theme.space05
            margins: 8   // keeps the menu inside the window near the bottom edge
            modal: false
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            background: Rectangle {
                radius: 0
                color: Theme.surface
                border.width: 1
                border.color: Theme.rule
            }
            contentItem: Column {
                spacing: 0
                Repeater {
                    model: root.items
                    delegate: Item {
                        id: entry
                        required property var modelData
                        objectName: root.objectName !== "" ? root.objectName + "_" + modelData.name : modelData.name
                        readonly property bool danger: modelData.danger === true
                        readonly property bool usable: modelData.enabled !== false
                        width: pop.availableWidth
                        height: 32 + (modelData.divider === true ? 9 : 0)
                        Rectangle {
                            visible: entry.modelData.divider === true
                            x: 0
                            y: 4
                            width: parent.width
                            height: 1
                            color: Theme.rule2
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 32
                            radius: 0
                            color: itemHover.hovered && entry.usable ? (entry.danger ? Theme.dangerTint : Theme.hover) : "transparent"
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.space15
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.space15
                                elide: Text.ElideRight
                                text: entry.modelData.label
                                textFormat: Text.PlainText
                                font.pixelSize: Theme.fontSmall
                                color: !entry.usable ? Theme.disabledInk : (entry.danger ? Theme.danger : Theme.ink)
                            }
                        }
                        HoverHandler { id: itemHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            enabled: entry.usable
                            onTapped: {
                                const chosen = entry.modelData.name
                                pop.close()
                                // After the click is handled: the action may rebuild the item list (and with it this delegate).
                                Qt.callLater(function() { root.activated(chosen) })
                            }
                        }
                    }
                }
            }
        }
    }
}
