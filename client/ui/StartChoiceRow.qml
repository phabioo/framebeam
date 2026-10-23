import QtQuick
import QtQuick.Layouts
import Uzume.App

// A numbered row of a short list of choices (8a-1 "Play on this PC / Join a server / Set up a server", 8a-4 step 1): number,
// title (20/28), text (14/22, at most 520 px) and a Mono meta line at the right (below the text in a narrow window). Hairline
// separators, hover fill, focus ring inside the row; Enter, Space and a click choose it.
Item {
    id: root
    property string number: ""
    property string title: ""
    property string text: ""
    property string meta: ""
    property bool primaryChoice: false       // the number and meta read `muted` instead of `dim` (the first row)
    readonly property bool narrow: width < 640
    signal activated()
    activeFocusOnTab: enabled
    implicitHeight: row.implicitHeight + 40
    implicitWidth: 480
    Layout.fillWidth: true
    Accessible.role: Accessible.Button
    Accessible.name: root.title + ". " + root.text
    Accessible.onPressAction: root.activated()

    Rectangle { anchors.fill: parent; color: hover.hovered && root.enabled ? Theme.hover : "transparent" }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule2 }
    GridLayout {
        id: row
        x: Theme.space2
        y: 20
        width: parent.width - 2 * Theme.space2
        columns: root.narrow ? 2 : 3
        columnSpacing: Theme.space3
        rowSpacing: Theme.space05
        Text {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 40
            Layout.rowSpan: root.narrow ? 2 : 1
            text: root.number
            textFormat: Text.PlainText
            font.family: Theme.mono
            font.pixelSize: Theme.roles.meta11.size
            lineHeight: 28
            lineHeightMode: Text.FixedHeight
            color: root.enabled ? (root.primaryChoice ? Theme.muted : Theme.dim) : Theme.disabledInk
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Theme.space05
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: root.title
                textFormat: Text.PlainText
                font.family: Theme.sans
                font.pixelSize: Theme.roles.dialogTitle.size
                font.letterSpacing: Theme.roles.dialogTitle.spacing
                lineHeight: Theme.roles.dialogTitle.line
                lineHeightMode: Text.FixedHeight
                color: root.enabled ? Theme.ink : Theme.disabledInk
            }
            Text {
                Layout.fillWidth: true
                Layout.maximumWidth: 520
                wrapMode: Text.Wrap
                text: root.text
                textFormat: Text.PlainText
                font.family: Theme.sans
                font.pixelSize: Theme.roles.body.size
                lineHeight: Theme.roles.body.line
                lineHeightMode: Text.FixedHeight
                color: root.enabled ? Theme.ink2 : Theme.disabledInk
            }
        }
        Text {
            visible: root.meta !== ""
            Layout.alignment: root.narrow ? Qt.AlignLeft : (Qt.AlignRight | Qt.AlignTop)
            Layout.preferredWidth: root.narrow ? -1 : 200
            Layout.topMargin: root.narrow ? 0 : 6
            horizontalAlignment: root.narrow ? Text.AlignLeft : Text.AlignRight
            wrapMode: Text.Wrap
            text: root.meta
            textFormat: Text.PlainText
            font.family: Theme.mono
            font.pixelSize: Theme.roles.meta.size
            font.letterSpacing: Theme.roles.meta.spacing
            font.capitalization: Font.AllUppercase
            color: root.enabled ? (root.primaryChoice ? Theme.muted : Theme.dim) : Theme.disabledInk
        }
    }
    HoverHandler { id: hover; cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    TapHandler { enabled: root.enabled; onTapped: { root.forceActiveFocus(); root.activated() } }
    Keys.onPressed: (event) => {
        if (root.enabled && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space)) {
            root.activated()
            event.accepted = true
        }
    }
    UzFocusRing { visible: root.activeFocus; inset: true }
}
