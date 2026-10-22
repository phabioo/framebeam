import QtQuick

// Avatar (design-system.md "Avatar"): a square in the person colour with the initial (on-person, 600), sizes 20 / 28 / 40.
// `person` is the join-order number 1..8 (Theme.playerColor) or 0-based colour index via `colorIndex`; `status` "online" (ok
// square) or "offline" (empty square) sits at the bottom right with a 2 px ground ring; `voice` (speaking | micOff | mutedByHost)
// adds the voice badge at the top right. Colour is never alone: the name is the accessible label.
Item {
    id: root
    property string name: ""
    property int person: 1               // 1..8, join order
    property int size: 28                // 20 | 28 | 40
    property string status: ""           // "" | online | offline
    property string voice: ""            // "" | speaking | micOff | mutedByHost
    property bool still: false
    readonly property bool large: size >= 40
    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    Accessible.role: Accessible.Graphic
    Accessible.name: [name, status, voice === "speaking" ? qsTr("speaking") : voice === "micOff" ? qsTr("mic off") : voice === "mutedByHost" ? qsTr("muted by host") : ""].filter(s => s !== "").join(", ")

    Rectangle {
        id: face
        objectName: "avatarFace"
        anchors.fill: parent
        radius: 0
        color: Theme.playerColor(root.person, Theme.dark)
        Text {
            anchors.centerIn: parent
            text: root.name !== "" ? root.name.charAt(0).toUpperCase() : "?"
            textFormat: Text.PlainText
            font.family: Theme.sans
            font.weight: Font.DemiBold
            font.pixelSize: root.large ? 16 : root.size >= 28 ? 12 : 10
            color: Theme.onPerson
        }
        Rectangle {   // speaking: inset Yamabuki ring with an on-accent line inside
            visible: root.voice === "speaking"
            anchors.fill: parent
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            Rectangle { anchors.fill: parent; anchors.margins: 2; color: "transparent"; border.width: 1; border.color: Theme.inkOnAccent }
        }
    }
    Rectangle {   // status square with a ground ring
        objectName: "avatarStatus"
        visible: root.status !== ""
        readonly property int s: root.large ? 10 : 8
        x: root.width - s + 3
        y: root.height - s + 3
        width: s
        height: s
        radius: 0
        color: root.status === "online" ? Theme.ok : Theme.ground
        border.width: root.status === "online" ? 0 : 1.5
        border.color: Theme.dim
        Rectangle { z: -1; anchors.fill: parent; anchors.margins: -2; color: Theme.ground }
    }
    Rectangle {   // voice badge
        objectName: "avatarVoice"
        visible: root.voice !== ""
        readonly property int s: root.large ? 18 : 14
        x: root.width - s + (root.large ? 5 : 4)
        y: root.large ? -5 : -4
        width: s
        height: s
        radius: 0
        color: root.voice === "mutedByHost" ? Theme.ink : Theme.ground
        border.width: 1
        border.color: root.voice === "speaking" ? Theme.accent : (root.voice === "mutedByHost" ? Theme.ground : Theme.rule)
        UzVoiceBars { visible: root.voice === "speaking"; still: root.still; anchors.centerIn: parent; scale: 0.8 }
        UzGlyph {
            visible: root.voice === "micOff" || root.voice === "mutedByHost"
            anchors.centerIn: parent
            name: "micOff"
            size: root.large ? 12 : 10
            color: root.voice === "mutedByHost" ? Theme.ground : Theme.muted
        }
    }
}
