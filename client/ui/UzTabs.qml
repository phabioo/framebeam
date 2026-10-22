import QtQuick

// Tabs (design-system.md "Tabs" and "RunTabs"): page tabs on a `rule-2` baseline; the active tab is ink 500 with the 2 px
// Yamabuki underline (marker-under), inactive tabs are muted. size "md" 36 px / 13 px text, "lg" 40 px / 14 px, "run" the 48 px
// tabs of the in-game side panel (gap 22, padding 24). items: list of labels; `active` the selected label; picked(label).
// Left / Right move between tabs, Home / End jump.
FocusScope {
    id: root
    property var items: []
    property string active: ""
    property string size: "md"       // md | lg | run
    signal picked(string label)

    activeFocusOnTab: true
    implicitHeight: size === "run" ? Theme.row48 : size === "lg" ? 40 : Theme.row36
    implicitWidth: row.implicitWidth + (size === "run" ? 2 * Theme.space3 : 0)
    Accessible.role: Accessible.PageTabList

    function step(delta) {
        const i = root.items.indexOf(root.active)
        const n = root.items.length
        if (n === 0) return
        const next = Math.max(0, Math.min(n - 1, (i < 0 ? 0 : i) + delta))
        if (root.items[next] !== root.active) root.picked(root.items[next])
    }
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Left) { step(-1); event.accepted = true }
        else if (event.key === Qt.Key_Right) { step(1); event.accepted = true }
        else if (event.key === Qt.Key_Home) { step(-1000); event.accepted = true }
        else if (event.key === Qt.Key_End) { step(1000); event.accepted = true }
    }

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule2 }
    Row {
        id: row
        x: root.size === "run" ? Theme.space3 : 0
        spacing: root.size === "run" ? 22 : root.size === "lg" ? 28 : Theme.space3
        Repeater {
            model: root.items
            delegate: Item {
                id: tab
                required property string modelData
                readonly property bool on: modelData === root.active
                objectName: "tab_" + modelData
                width: tabLabel.implicitWidth
                height: root.height
                Text {
                    id: tabLabel
                    anchors.verticalCenter: parent.verticalCenter
                    text: tab.modelData
                    textFormat: Text.PlainText
                    font.family: Theme.sans
                    font.pixelSize: root.size === "lg" ? 14 : 13
                    font.weight: tab.on ? Font.Medium : Font.Normal
                    color: tab.on || hover.hovered ? Theme.ink : Theme.muted
                }
                Rectangle {   // marker-under
                    visible: tab.on
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 2
                    color: Theme.accent
                }
                Accessible.role: Accessible.PageTab
                Accessible.name: tab.modelData
                Accessible.selected: tab.on
                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.picked(tab.modelData) }
            }
        }
    }
    UzFocusRing { visible: root.activeFocus }
}
