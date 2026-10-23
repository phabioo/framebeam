import QtQuick
import QtQuick.Layouts
import Uzume.App

// 8d-5 Switch while a game runs: "Quit the game first". Uzume saves the running game, uploads the save, then shows who is playing
// (it never switches with an unsaved game). Save, quit and switch (primary) / Keep playing.
StartPage {
    id: root
    required property PlayerController player
    readonly property var ac: player.accounts
    screenLabel: qsTr("Switch user · game running")
    openLabel: qsTr("UZUME · SWITCH USER")
    initialFocus: go
    hints: [{ kbd: "Enter", pad: "A", label: qsTr("Choose") }, { kbd: "Esc", pad: "B", label: qsTr("Keep playing") }]
    onBackRequested: ac.keepPlaying()

    StartHead {
        number: 1
        label: qsTr("Switch user")
        title: qsTr("Quit the game first")
        text: qsTr("Uzume saves the running game, uploads your save, then shows who is playing.")
    }
    ColumnLayout {
        objectName: "quitSteps"
        Layout.fillWidth: true
        spacing: 0
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.rule }
        Repeater {
            model: [{ n: "01", t: qsTr("Save the running game") },
                    { n: "02", t: qsTr("Upload to %1").arg(root.ac.serverName) },
                    { n: "03", t: qsTr("Switch user") }]
            delegate: Item {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: Theme.row44
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule2 }
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    x: Theme.space1
                    spacing: Theme.space3
                    Text { width: 24; text: modelData.n; textFormat: Text.PlainText; font.family: Theme.mono; font.pixelSize: Theme.roles.meta11.size; color: Theme.dim }
                    Text { text: modelData.t; textFormat: Text.PlainText; font.family: Theme.sans; font.pixelSize: Theme.roles.body.size; color: Theme.ink }
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space1
        UzButton { id: go; objectName: "quitAndSwitch"; kind: "primary"; busyOnClick: true; text: qsTr("Save, quit and switch"); onClicked: root.ac.confirmQuitAndSwitch() }
        UzButton { objectName: "quitKeepPlaying"; text: qsTr("Keep playing"); onClicked: root.ac.keepPlaying() }
        Item { Layout.fillWidth: true }
    }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: qsTr("Offline, the save stays on this PC under the first person and uploads at their next sign-in. Others cannot open it.")
        textFormat: Text.PlainText
        font.family: Theme.sans
        font.pixelSize: Theme.roles.small.size
        lineHeight: Theme.roles.small.line
        lineHeightMode: Text.FixedHeight
        color: Theme.muted
    }
}
