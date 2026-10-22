import QtQuick

// Lockup (frame.md "0-4 A"; bundle `Lockup`): the mark and the wordmark "Uzume" (Plex Sans 600, no suffix).
// nav (default): mark 20, wordmark 17/20 -0.01em, gap 10, for the sidebar and the no-sidebar first-start screens.
// display: mark 52, wordmark 46/46 -0.035em, gap 16, with the optional ウズメ (`jp`, muted, only on About, splash and the
// installer welcome).
Row {
    id: root
    property string variant: "nav"     // nav | display
    property bool jp: false
    readonly property bool display: variant === "display"
    spacing: display ? Theme.space2 : Theme.space1 + Theme.space05 / 2   // 10 and 16

    UzMark {
        size: root.display ? 52 : Theme.markNav
        anchors.verticalCenter: parent.verticalCenter
    }
    Text {
        objectName: "uzWordmark"
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Uzume")
        textFormat: Text.PlainText
        color: Theme.ink
        font.family: Theme.sans
        font.weight: Font.DemiBold
        font.pixelSize: root.display ? Theme.roles.wordmarkDisplay.size : Theme.roles.wordmarkNav.size
        font.letterSpacing: root.display ? Theme.roles.wordmarkDisplay.spacing : Theme.roles.wordmarkNav.spacing
    }
    Text {
        visible: root.display && root.jp
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 7
        text: "ウズメ"
        textFormat: Text.PlainText
        color: Theme.muted
        font.family: Theme.jp
        font.pixelSize: Theme.roles.jpAccent.size
    }
}
