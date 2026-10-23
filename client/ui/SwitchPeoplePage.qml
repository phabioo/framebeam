import QtQuick
import QtQuick.Layouts
import Uzume.App

// 8d-1 / 8d-4 Switch user: "Who's playing?" Tiles of the people on this PC (avatar 40, name, last used, PIN tag) and
// "Add user / Sign in someone else". Enter or a click switches, Del (or "Remove from this PC") removes the person from this PC.
StartPage {
    id: root
    required property PlayerController player
    readonly property var ac: player.accounts
    readonly property var people: ac.people
    readonly property int tileCount: people.length + 1
    maxWidth: 1040
    spacing: 40
    screenLabel: qsTr("%1 · %2").arg(player.deviceName).arg(ac.serverName)
    openLabel: qsTr("UZUME · SWITCH USER")
    hints: [{ kbd: "← →", pad: "LeftRight", label: qsTr("Move") },
            { kbd: "Enter", pad: "A", label: qsTr("Choose") },
            { kbd: "Del", pad: "X", label: qsTr("Remove from this PC") },
            { kbd: "Esc", pad: "B", label: qsTr("Back") }]
    onBackRequested: ac.close()

    // Stable person colour (join order is not known here): the same person always gets the same one of the four.
    function personIndex(id: string): int {
        let h = 0
        for (let i = 0; i < id.length; ++i) h = (h * 31 + id.charCodeAt(i)) % 4099
        return 1 + h % 4
    }
    function metaOf(p): string {
        const when = p.lastUsed !== "" ? root.player.whenText(p.lastUsed) : ""
        if (p.isAdmin) return when !== "" ? qsTr("Admin · %1").arg(when) : qsTr("Admin")
        return when !== "" ? qsTr("Last used %1").arg(when) : qsTr("Not used yet")
    }
    function focusTile(i: int) {
        const n = root.tileCount
        const item = tiles.itemAt(((i % n) + n) % n)
        if (item) item.forceActiveFocus()
    }

    StartHead {
        number: 1
        label: qsTr("Switch user")
        meta: qsTr("%1 on this PC").arg(Theme.plural(root.people.length, qsTr("person"), qsTr("people")))
        title: qsTr("Who’s playing?")
        text: qsTr("People signed in on %1. Saves always go to the person who is signed in.").arg(root.player.deviceName)
    }
    Flow {
        objectName: "peopleTiles"
        Layout.fillWidth: true
        spacing: Theme.space3
        Repeater {
            id: tiles
            model: root.people.concat([{ add: true }])
            onItemAdded: (index, item) => { if (index === 0) root.initialFocus = item }
            delegate: Item {
                id: tile
                required property var modelData
                required property int index
                readonly property bool isAdd: modelData.add === true
                objectName: isAdd ? "personAdd" : "personTile_" + modelData.userId
                width: 200
                height: tileCol.implicitHeight + 40
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: isAdd ? qsTr("Add user. Sign in someone else") : modelData.name + (modelData.hasPin ? ", " + qsTr("PIN") : "")
                Accessible.onPressAction: tile.activate()
                function activate() { isAdd ? root.ac.addPerson() : root.ac.switchTo(modelData.userId) }

                Rectangle { anchors.fill: parent; color: hover.hovered ? Theme.hover : "transparent" }
                Rectangle { width: parent.width; height: 1; color: Theme.rule }
                Rectangle {   // the person-colour rule, 40 x 3
                    visible: !tile.isAdd
                    width: Theme.personMarker
                    height: 3
                    color: tile.isAdd ? "transparent" : Theme.playerColor(root.personIndex(tile.modelData.userId), Theme.dark)
                }
                ColumnLayout {
                    id: tileCol
                    x: Theme.space2
                    y: 20
                    width: parent.width - 2 * Theme.space2
                    spacing: Theme.space15
                    UzAvatar {
                        visible: !tile.isAdd
                        name: tile.isAdd ? "" : tile.modelData.name
                        person: tile.isAdd ? 1 : root.personIndex(tile.modelData.userId)
                        size: 40
                        status: !tile.isAdd && tile.modelData.isActive ? "online" : ""
                    }
                    Rectangle {
                        visible: tile.isAdd
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.rule
                        Text { anchors.centerIn: parent; text: "+"; font.family: Theme.mono; font.pixelSize: 16; color: Theme.muted }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: tile.isAdd ? qsTr("Add user") : tile.modelData.name
                            textFormat: Text.PlainText
                            font.family: Theme.sans
                            font.pixelSize: Theme.roles.itemTitle.size
                            font.weight: Font.Medium
                            color: Theme.ink
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: tile.isAdd ? qsTr("Sign in someone else") : root.metaOf(tile.modelData)
                            textFormat: Text.PlainText
                            font.family: Theme.sans
                            font.pixelSize: Theme.roles.small.size
                            color: Theme.muted
                        }
                    }
                    Flow {
                        visible: !tile.isAdd && (tile.modelData.hasPin || tile.modelData.pendingSaves > 0 || !tile.modelData.signedIn)
                        Layout.fillWidth: true
                        spacing: Theme.space1
                        UzTag { objectName: "personPin"; visible: tile.modelData.hasPin === true; text: tile.modelData.pinLocked ? qsTr("PIN locked") : qsTr("PIN") }
                        UzTag { visible: tile.modelData.pendingSaves > 0; tone: "warn"; text: qsTr("Saves waiting") }
                        UzTag { visible: tile.modelData.signedIn === false; text: qsTr("Password needed") }
                    }
                    UzButton {
                        objectName: tile.isAdd ? "" : "personRemove_" + tile.modelData.userId
                        visible: !tile.isAdd
                        kind: "link"
                        size: "sm"
                        focusPolicy: Qt.NoFocus
                        text: qsTr("Remove from this PC")
                        onClicked: root.ac.removeFromThisPc(tile.modelData.userId)
                    }
                }
                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: { tile.forceActiveFocus(); tile.activate() } }
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                        tile.activate()
                    } else if (event.key === Qt.Key_Delete && !tile.isAdd) {
                        root.ac.removeFromThisPc(tile.modelData.userId)
                    } else if (event.key === Qt.Key_Left) {
                        root.focusTile(tile.index - 1)
                    } else if (event.key === Qt.Key_Right) {
                        root.focusTile(tile.index + 1)
                    } else {
                        return
                    }
                    event.accepted = true
                }
                UzFocusRing { visible: tile.activeFocus; inset: true }
            }
        }
    }
    UzStatusIcon {
        objectName: "peopleError"
        visible: root.ac.errorMessage !== ""
        kind: "error"
        small: true
        text: root.ac.errorMessage
        maxWidth: 480
    }
    RowLayout {
        objectName: "peopleOnlyOne"
        visible: root.people.length === 1
        Layout.fillWidth: true
        spacing: Theme.space1
        Text {
            text: qsTr("Only one person is signed in on this PC. Add someone to share it. Ctrl+Shift+U opens this screen.")
            textFormat: Text.PlainText
            font.family: Theme.sans
            font.pixelSize: Theme.roles.small.size
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
