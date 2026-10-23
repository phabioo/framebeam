import QtQuick
import Uzume.App

// Small in-game menu for the plugin surfaces (HUD "⋯", notice "⋯", tile "⋯"; docs/design/legacy/plugins.md 4e, 4g-2): `surface` plane, `rule` frame, 32 px items. Fills its parent as a click-away scrim and places the menu next to
// `anchorItem`, kept inside the parent. Items: {id, text, trailing?, checked?, divider?} or a row of glyph choices
// {id, text, choices: [{id, glyph, tip}]}; `picked(id)` is emitted with the item id (or the choice id).
Item {
    id: root
    property var items: []
    property Item anchorItem: null
    property bool open: false
    property real menuWidth: 250
    property bool above: false            // open upwards (anchors near the bottom edge)
    signal picked(string id)
    signal closeRequested()

    z: 80
    anchors.fill: parent
    visible: open && anchorItem !== null

    property real ax: 0
    property real ay: 0
    property real aw: 0
    property real ah: 0
    function reposition() {
        if (!anchorItem) return
        const p = anchorItem.mapToItem(root, 0, 0)
        ax = p.x; ay = p.y; aw = anchorItem.width; ah = anchorItem.height
    }
    onOpenChanged: if (open) reposition()
    onAnchorItemChanged: if (open) reposition()

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: (m) => { m.accepted = true; root.closeRequested() }
    }

    Rectangle {
        id: box
        objectName: "pluginMenu"
        width: root.menuWidth
        height: col.height + 2 * Theme.space05
        radius: 0
        color: Theme.surface
        border.width: 1
        border.color: Theme.rule
        x: Math.max(Theme.space1, Math.min(root.width - width - Theme.space1, root.ax + root.aw - width))
        y: root.above ? Math.max(Theme.space1, root.ay - height - Theme.space1) : Math.min(root.height - height - Theme.space1, root.ay + root.ah + Theme.space1)
        Column {
            id: col
            x: Theme.space05; y: Theme.space05
            width: parent.width - 2 * Theme.space05
            Repeater {
                model: root.items
                delegate: Item {
                    id: row
                    required property var modelData
                    readonly property bool hasChoices: modelData.choices !== undefined
                    objectName: "pluginMenuItem_" + modelData.id
                    width: col.width
                    height: Theme.row32 + (modelData.divider ? 5 : 0)
                    Rectangle {
                        visible: row.modelData.divider === true
                        x: 4; y: 2
                        width: parent.width - 8; height: 1
                        color: Theme.rule2
                    }
                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: row.modelData.divider ? 5 : 0
                        radius: 0
                        color: !row.hasChoices && itemHover.hovered ? Theme.pressed : "transparent"
                        UzLabel {
                            textFormat: Text.PlainText  // plugin- and peer-supplied strings are never HTML
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.space15
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 2 * Theme.space15 - (row.hasChoices ? choiceRow.width : trailingText.implicitWidth + 8)
                            elide: Text.ElideRight
                            text: row.modelData.text
                            font.pixelSize: Theme.roles.input.size
                            color: row.modelData.checked ? Theme.ink : Theme.ink2
                            font.weight: row.modelData.checked ? Font.Medium : Font.Normal
                        }
                        UzMono {
                            textFormat: Text.PlainText
                            id: trailingText
                            visible: !row.hasChoices
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.space15
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.trailing || (row.modelData.checked ? "✓" : "")
                            font.pixelSize: Theme.roles.meta11.size
                            color: row.modelData.checked && !row.modelData.trailing ? Theme.accentText : Theme.dim
                        }
                        Row {
                            id: choiceRow
                            visible: row.hasChoices
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Repeater {
                                model: row.hasChoices ? row.modelData.choices : []
                                delegate: Rectangle {
                                    id: choice
                                    required property var modelData
                                    objectName: "pluginMenuChoice_" + modelData.id
                                    width: 26; height: 24; radius: 0
                                    color: choiceHover.hovered ? Theme.pressed : "transparent"
                                    Accessible.role: Accessible.Button
                                    Accessible.name: modelData.tip || modelData.glyph
                                    Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: choice.modelData.glyph; font.pixelSize: 14; color: choice.modelData.checked ? Theme.accentText : Theme.ink }
                                    HoverHandler { id: choiceHover; cursorShape: Qt.PointingHandCursor }
                                    TapHandler { onTapped: root.picked(choice.modelData.id) }
                                }
                            }
                        }
                        HoverHandler { id: itemHover; enabled: !row.hasChoices; cursorShape: Qt.PointingHandCursor }
                        TapHandler { enabled: !row.hasChoices; onTapped: root.picked(row.modelData.id) }
                    }
                }
            }
        }
    }
}
