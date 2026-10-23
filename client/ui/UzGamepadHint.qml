import QtQuick

// GamepadHint (design-system.md "GamepadHint"): a square key frame with a Mono letter and an optional label. Keys: A B X Y L R
// L2 R2 L3 R3 Select Start and the D-pad directions (↑ ↓ ← → or Up Down Left Right, UpDown and LeftRight as pairs). `state`
// "pressed" fills the key, `disabled` dims it. The controller glyph sets of the Controllers page stay in PadGlyph.
Row {
    id: root
    property string k: "A"
    property string label: ""
    property bool pressed: false
    property bool disabled: false
    readonly property var dpad: ({ "Up": "up", "Down": "down", "Left": "left", "Right": "right",
                                   "↑": "up", "↓": "down", "←": "left", "→": "right",
                                   "UpDown": "up/down", "LeftRight": "left/right" })
    readonly property var dpadNames: ({ "up": qsTr("D-pad up"), "down": qsTr("D-pad down"), "left": qsTr("D-pad left"), "right": qsTr("D-pad right"),
                                        "up/down": qsTr("D-pad up or down"), "left/right": qsTr("D-pad left or right") })
    readonly property string glyph: root.dpad[root.k] || ""
    spacing: 6
    Accessible.role: Accessible.StaticText
    Accessible.name: (root.glyph !== "" ? root.dpadNames[root.glyph] : qsTr("%1 button").arg(root.k)) + (root.label !== "" ? " " + root.label : "")

    UzKeyCap {
        objectName: "gamepadKey"
        anchors.verticalCenter: parent.verticalCenter
        text: root.glyph === "" ? root.k : ""
        glyph: root.glyph
        pressed: root.pressed
        disabled: root.disabled
    }
    Text {
        visible: root.label !== ""
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        textFormat: Text.PlainText
        font.family: Theme.sans
        font.pixelSize: Theme.roles.small.size
        color: root.disabled ? Theme.disabledInk : Theme.muted
    }
}
