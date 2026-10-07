import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// One collapsible section of the diagnostics overlay: caret, title (mono eyebrow), and, while collapsed, the summary line.
// The body is the default content; a click on the header toggles.
ColumnLayout {
    id: root
    property string title: ""
    property string summary: ""
    property bool expanded: true
    property real indent: 0           // body indent (multiview panel: 18)
    default property alias content: body.data
    signal toggled()
    spacing: 10

    Item {
        id: header
        objectName: "diagSectionHeader"
        Layout.fillWidth: true
        implicitHeight: 18
        RowLayout {
            anchors.fill: parent
            spacing: 8
            FbMono { text: root.expanded ? "▾" : "▸"; font.pixelSize: 11; color: Theme.textFaint }
            FbMono {
                text: root.title
                font.pixelSize: 11
                font.weight: Font.Medium
                font.letterSpacing: 0.9
                color: Theme.textFaint
            }
            Item { Layout.fillWidth: true }
            FbMono {
                objectName: "diagSectionSummary"
                visible: !root.expanded
                text: root.summary
                font.pixelSize: 11
                color: Theme.textMeta
            }
        }
        TapHandler { onTapped: root.toggled() }
    }

    ColumnLayout {
        id: body
        visible: root.expanded
        Layout.fillWidth: true
        Layout.leftMargin: root.indent
        spacing: 8
    }
}
