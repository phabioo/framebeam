import QtQuick
import QtQuick.Layouts
import Uzume.App

// PIN entry of 8d-2 / 8d-3: four square digits that fill in ink (never round) and a keypad of 3 x 4 keys (72 x 56, 8 px gaps):
// 1-9, delete, 0, OK (the one Yamabuki key). Number keys, Backspace and Enter work on a keyboard; the arrows move the
// highlighted key (D-pad) and Space presses it. `submitted(pin)` fires on OK / Enter with all four digits.
FocusScope {
    id: root
    property int length: 4
    property string entered: ""
    property int cur: 4                         // highlighted key (index in `keys`): 5
    readonly property var keys: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "del", "0", "ok"]
    signal submitted(string pin)
    implicitWidth: 3 * 72 + 2 * Theme.space1
    implicitHeight: dots.height + 28 + pad.height
    activeFocusOnTab: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("PIN pad, %1 of %2 digits entered").arg(root.entered.length).arg(root.length)

    function press(key: string) {
        if (!root.enabled) return
        if (key === "del") root.entered = root.entered.slice(0, -1)
        else if (key === "ok") { if (root.entered.length === root.length) root.submitted(root.entered) }
        else if (root.entered.length < root.length) root.entered += key
    }
    function clear() { root.entered = "" }

    Row {
        id: dots
        objectName: "pinDots"
        spacing: Theme.space2
        Repeater {
            model: root.length
            delegate: Rectangle {
                required property int index
                width: 16
                height: 16
                radius: 0
                color: index < root.entered.length ? Theme.ink : "transparent"
                border.width: index < root.entered.length ? 0 : 1
                border.color: Theme.rule
            }
        }
    }
    Grid {
        id: pad
        objectName: "pinKeys"
        y: dots.height + 28
        columns: 3
        columnSpacing: Theme.space1
        rowSpacing: Theme.space1
        Repeater {
            model: root.keys
            delegate: Item {
                id: key
                required property string modelData
                required property int index
                readonly property bool isOk: modelData === "ok"
                objectName: "pinKey_" + modelData
                width: 72
                height: 56
                Rectangle {
                    anchors.fill: parent
                    radius: 0
                    color: key.isOk ? (tap.pressed ? Theme.accentPressed : (hover.hovered ? Theme.accentHover : Theme.accent))
                                    : (tap.pressed ? Theme.pressed : (hover.hovered || (root.activeFocus && root.cur === key.index) ? Theme.hover : "transparent"))
                    border.width: key.isOk ? 0 : 1
                    border.color: hover.hovered ? Theme.ink2 : Theme.rule
                    opacity: root.enabled ? 1 : 0.5
                }
                Text {
                    anchors.centerIn: parent
                    text: key.modelData === "del" ? "⌫" : key.modelData === "ok" ? qsTr("OK") : key.modelData
                    textFormat: Text.PlainText
                    font.family: key.isOk ? Theme.sans : Theme.mono
                    font.weight: Font.Medium
                    font.pixelSize: key.modelData === "del" ? 16 : (key.isOk ? 14 : 20)
                    color: key.isOk ? Theme.inkOnAccent : (key.modelData === "del" ? Theme.muted : Theme.ink)
                }
                UzFocusRing { visible: root.activeFocus && root.cur === key.index }
                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { id: tap; onTapped: { root.cur = key.index; root.forceActiveFocus(); root.press(key.modelData) } }
                Accessible.role: Accessible.Button
                Accessible.name: key.modelData === "del" ? qsTr("Delete") : key.modelData === "ok" ? qsTr("OK") : key.modelData
                Accessible.onPressAction: root.press(key.modelData)
            }
        }
    }
    Keys.onPressed: (event) => {
        if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9 && (event.modifiers & ~Qt.KeypadModifier) === 0) {
            root.press(String(event.key - Qt.Key_0))
        } else if (event.key === Qt.Key_Backspace) {
            root.press("del")
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.press("ok")
        } else if (event.key === Qt.Key_Space) {
            root.press(root.keys[root.cur])
        } else if (event.key === Qt.Key_Left) {
            root.cur = (root.cur + 11) % 12
        } else if (event.key === Qt.Key_Right) {
            root.cur = (root.cur + 1) % 12
        } else if (event.key === Qt.Key_Up) {
            root.cur = (root.cur + 9) % 12
        } else if (event.key === Qt.Key_Down) {
            root.cur = (root.cur + 3) % 12
        } else {
            return
        }
        event.accepted = true
    }
}
