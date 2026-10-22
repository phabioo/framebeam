import QtQuick

// Page header (design-system.md "PageHeader"): the section label above the page title (Plex Sans Light 40/40, -2 %, once per
// page), with the page actions (buttons) at the far right, aligned to the title baseline. Actions go into the default slot.
Item {
    id: root
    property int number: -1
    property string label: ""
    property string title: ""
    default property alias actions: actionRow.data
    implicitHeight: col.implicitHeight
    implicitWidth: col.implicitWidth + actionRow.implicitWidth

    Column {
        id: col
        anchors.left: parent.left
        anchors.right: actionRow.left
        anchors.rightMargin: Theme.space3
        spacing: Theme.space1
        UzSectionLabel { width: parent.width; number: root.number; text: root.label !== "" ? root.label : root.title }
        Text {
            objectName: "pageTitle"
            width: parent.width
            elide: Text.ElideRight
            text: root.title
            textFormat: Text.PlainText
            font.family: Theme.sans
            font.pixelSize: Theme.roles.title.size
            font.weight: Font.Light
            font.letterSpacing: Theme.roles.title.spacing
            color: Theme.ink
            lineHeight: Theme.roles.title.line
            lineHeightMode: Text.FixedHeight
        }
    }
    Row {
        id: actionRow
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: Theme.space2
    }
}
