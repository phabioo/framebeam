import QtQuick

// Register marks (design-system.md "Register marks"): four 1 px corner marks with 12 px arms (Theme.registerMark) framing a
// content area, `rule` on pages and `dim` around the game stage (`tone: "dim"`). `inset` is the distance from the edges
// (24 by default, negative to sit outside). Fills its parent, never takes input.
Item {
    id: root
    property int inset: Theme.space3
    property string tone: "rule"     // rule | dim
    readonly property color markColor: tone === "dim" ? Theme.dim : Theme.rule
    anchors.fill: parent
    enabled: false
    Accessible.ignored: true

    Repeater {
        model: [{ n: "TL", l: true, t: true }, { n: "TR", l: false, t: true }, { n: "BL", l: true, t: false }, { n: "BR", l: false, t: false }]
        delegate: Item {
            id: m
            required property var modelData
            objectName: "registerMark" + modelData.n
            width: Theme.registerMark
            height: Theme.registerMark
            x: modelData.l ? root.inset : root.width - root.inset - width
            y: modelData.t ? root.inset : root.height - root.inset - height
            Rectangle {   // horizontal arm, along the outer edge of the corner
                x: 0; y: m.modelData.t ? 0 : parent.height - 1
                width: parent.width; height: 1
                color: root.markColor
            }
            Rectangle {   // vertical arm
                x: m.modelData.l ? 0 : parent.width - 1; y: 0
                width: 1; height: parent.height
                color: root.markColor
            }
        }
    }
}
