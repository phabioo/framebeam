import QtQuick

// Progress (design-system.md "Progress"): a 4 px bar (`thin`: 2 px) on a `rule-2` track with a Yamabuki fill; optional label
// above and hint (Mono) to the right of it. value 0..1. Also the microphone level.
Item {
    id: root
    property real value: 0
    property bool thin: false
    property string label: ""
    property string hint: ""
    readonly property real clamped: Math.max(0, Math.min(1, value))
    implicitWidth: 240
    implicitHeight: head.visible ? head.height + 6 + bar.height : bar.height
    Accessible.role: Accessible.ProgressBar
    Accessible.name: root.label
    Accessible.description: Math.round(root.clamped * 100) + " %"

    Item {
        id: head
        visible: root.label !== "" || root.hint !== ""
        width: parent.width
        height: visible ? 16 : 0
        Text {
            anchors.left: parent.left
            anchors.right: hintText.left
            anchors.rightMargin: Theme.space1
            elide: Text.ElideRight
            text: root.label
            textFormat: Text.PlainText
            font.family: Theme.sans
            font.pixelSize: Theme.roles.small.size
            color: Theme.ink
        }
        Text {
            id: hintText
            anchors.right: parent.right
            text: root.hint
            textFormat: Text.PlainText
            font.family: Theme.mono
            font.pixelSize: 11
            color: Theme.muted
        }
    }
    Rectangle {
        id: bar
        objectName: "progressTrack"
        y: head.visible ? head.height + 6 : 0
        width: parent.width
        height: root.thin ? 2 : 4
        radius: 0
        color: Theme.rule2
        Rectangle {
            objectName: "progressFill"
            width: parent.width * root.clamped
            height: parent.height
            radius: 0
            color: Theme.accent
        }
    }
}
