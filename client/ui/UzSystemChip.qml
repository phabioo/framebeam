import QtQuick

// SystemChip (design-system.md "SystemChip"): the Mono system code chip, always one of GB, GBC, GBA, NDS, 3DS: 1 px `rule`
// frame, padding 2 / 6, `ink-2` text on the ground.
Rectangle {
    id: root
    property string system: ""
    implicitWidth: label.implicitWidth + 12 + 2
    implicitHeight: 14 + 4 + 2
    radius: 0
    color: Theme.ground
    border.width: 1
    border.color: Theme.rule
    Text {
        id: label
        anchors.centerIn: parent
        text: root.system
        textFormat: Text.PlainText
        font.family: Theme.mono
        font.pixelSize: Theme.roles.systemCode.size
        font.weight: Font.Medium
        font.letterSpacing: Theme.roles.systemCode.spacing
        font.capitalization: Font.AllUppercase
        color: Theme.ink2
    }
}
