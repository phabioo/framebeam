import QtQuick

// Hint row (design-system.md "Focus is always visible"): the bottom row of a screen, 44 px with a `rule-2` line above, showing
// the keys of the last input device: GamepadHint glyphs for `device: "gamepad"`, Kbd keys for "keyboard". The app does not
// track the last input device yet, so the owner sets `device`.
// hints: [{ pad: "A", kbd: "Enter", label: qsTr("Choose") }, ...]  (a hint without kbd / pad is left out for that device)
Item {
    id: root
    property string device: "keyboard"     // keyboard | gamepad
    property var hints: []
    implicitHeight: 44
    implicitWidth: row.implicitWidth + 2 * Theme.space3

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.rule2 }
    Row {
        id: row
        objectName: "hintRowKeys"
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: Theme.space3
        spacing: Theme.space2
        Repeater {
            model: root.hints.filter(h => root.device === "gamepad" ? h.pad !== undefined : h.kbd !== undefined)
            delegate: Loader {
                required property var modelData
                sourceComponent: root.device === "gamepad" ? padComp : kbdComp
                Component { id: padComp; UzGamepadHint { k: modelData.pad; label: modelData.label || "" } }
                Component { id: kbdComp; UzKbd { keys: modelData.kbd; label: modelData.label || "" } }
            }
        }
    }
}
