import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// One row of the save timeline (docs/design/source/save-row.dc.html, dark; light follows Theme): rail 20 | text | actions.
// kind: "current" (filled node, "✓ Current"), "snap" (diamond, "◆ Snapshot") or "plain" (hollow node).
// The confirmation opens under the row, indented 32: confirm = "restore" (accent box) or "delete" (danger box).
ColumnLayout {
    id: root
    property string kind: "plain"
    property string versionText: ""
    property string label: ""
    property string meta: ""
    property bool showRestore: true
    property bool showDelete: false
    property bool restoreEnabled: true
    property bool deleteEnabled: true
    property bool muted: false            // read-only (Hub offline, restore blocked): the actions look disabled
    property string confirm: ""           // "" | "restore" | "delete"
    property string confirmTitle: ""
    property string confirmBody: ""
    property string confirmOk: ""
    property bool last: false
    signal restoreClicked()
    signal deleteClicked()
    signal confirmed()
    signal cancelled()

    readonly property bool isCurrent: kind === "current"
    readonly property bool isSnap: kind === "snap"

    objectName: "saveRow"
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    spacing: 0

    // Version row
    Item {
        id: versionRow
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: Math.max(textCol.implicitHeight + 7 + 9, 40)

        // Rail with node
        Item {
            id: rail
            width: 20
            height: parent.height
            Rectangle { x: 9.5; width: 1; height: 14; color: Theme.rail; visible: true }
            Rectangle { x: 9.5; y: 14 + 12; width: 1; height: parent.height - 26; color: Theme.rail; visible: !root.last }
            // current: 12px filled with a 3px gap ring and a 1px accent ring
            Rectangle {
                visible: root.isCurrent
                x: 4; y: 14; width: 12; height: 12; radius: 6
                color: Theme.accent
                Rectangle { anchors.centerIn: parent; width: 18; height: 18; radius: 9; color: "transparent"; border.width: 1; border.color: Theme.accent }
            }
            // snapshot: 9px diamond
            Rectangle {
                visible: root.isSnap
                x: 5.5; y: 15.5; width: 9; height: 9
                rotation: 45
                color: Theme.accent
            }
            // other: 10px hollow, 2px border
            Rectangle {
                visible: !root.isCurrent && !root.isSnap
                x: 5; y: 15; width: 10; height: 10; radius: 5
                color: Theme.bgPanel
                border.width: 2
                border.color: Theme.textTile
            }
        }

        // Text
        ColumnLayout {
            id: textCol
            anchors.left: rail.right
            anchors.leftMargin: 12
            anchors.right: actions.left
            anchors.rightMargin: 8
            y: 7
            spacing: 3
            Flow {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 8
                FbMono {
                    objectName: "historyTitle"
                    text: root.versionText
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Medium
                }
                Rectangle {
                    visible: root.isCurrent
                    height: 20; width: curText.implicitWidth + 16; radius: 10
                    color: Theme.okBg
                    Text { id: curText; anchors.centerIn: parent; text: qsTr("✓ Current"); font.pixelSize: Theme.fontMono; font.weight: Font.DemiBold; color: Theme.ok }
                }
                Rectangle {
                    visible: root.isSnap
                    height: 20; width: snapText.implicitWidth + 16; radius: 10
                    color: Theme.accentChipBg
                    Text { id: snapText; anchors.centerIn: parent; text: qsTr("◆ Snapshot"); font.pixelSize: Theme.fontMono; font.weight: Font.DemiBold; color: Theme.accent }
                }
                FbLabel {
                    visible: root.label !== ""
                    objectName: "historyLabel"
                    width: Math.min(implicitWidth, textCol.width)
                    text: "“" + root.label + "”"
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
            }
            FbLabel {
                objectName: "historyMeta"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.meta
                color: Theme.textMeta
                font.pixelSize: Theme.fontMeta
                wrapMode: Text.WordWrap
            }
        }

        // Actions
        Row {
            id: actions
            anchors.right: parent.right
            y: 6
            spacing: 6
            Rectangle {
                objectName: "restoreButton"
                visible: root.showRestore && !root.isCurrent
                enabled: root.restoreEnabled
                height: 28; width: restoreText.implicitWidth + 20; radius: Theme.radius6
                color: restoreHover.hovered && enabled ? Theme.surfaceRaised : "transparent"
                border.width: 1
                border.color: root.confirm === "restore" ? Theme.accent : Theme.borderButton
                opacity: enabled && !root.muted ? 1 : 0.5
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Restore %1").arg(root.versionText)
                Text { id: restoreText; anchors.centerIn: parent; text: qsTr("Restore"); font.pixelSize: Theme.fontMeta; font.weight: Font.Medium; color: Theme.text }
                HoverHandler { id: restoreHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { enabled: parent.enabled; onTapped: root.restoreClicked() }
                Keys.onPressed: (event) => { if (enabled && (event.key === Qt.Key_Space || event.key === Qt.Key_Return)) { root.restoreClicked(); event.accepted = true } }
            }
            Rectangle {
                objectName: "deleteButton"
                visible: root.showDelete
                enabled: root.deleteEnabled
                height: 28; width: deleteText.implicitWidth + 12; radius: Theme.radius6
                color: deleteHover.hovered && enabled ? Theme.surfaceRaised : "transparent"
                opacity: enabled && !root.muted ? 1 : 0.5
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Delete snapshot %1").arg(root.versionText)
                Text { id: deleteText; anchors.centerIn: parent; text: qsTr("Delete"); font.pixelSize: Theme.fontMeta; font.weight: Font.Medium; color: Theme.error }
                HoverHandler { id: deleteHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { enabled: parent.enabled; onTapped: root.deleteClicked() }
                Keys.onPressed: (event) => { if (enabled && (event.key === Qt.Key_Space || event.key === Qt.Key_Return)) { root.deleteClicked(); event.accepted = true } }
            }
        }

        Rectangle { anchors.bottom: parent.bottom; anchors.left: rail.right; anchors.leftMargin: 12; anchors.right: parent.right; height: 1; color: Theme.borderSidebar }
    }

    // Confirmation under the row
    Rectangle {
        id: confirmBox
        objectName: root.confirm === "delete" ? "deleteConfirmBox" : "restoreConfirmBox"
        visible: root.confirm !== ""
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.topMargin: 8
        Layout.bottomMargin: 10
        Layout.leftMargin: 32
        implicitHeight: confirmCol.implicitHeight + 24
        radius: Theme.radius9
        color: root.confirm === "delete" ? Theme.errorBg : Theme.accentChipBg
        border.width: 1
        border.color: root.confirm === "delete" ? Theme.dangerBorder : Theme.confirmBorder
        ColumnLayout {
            id: confirmCol
            anchors.fill: parent
            anchors.margins: 12
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 3
                FbLabel {
                    objectName: "confirmTitle"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.confirmTitle
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    color: root.confirm === "delete" ? Theme.error : Theme.accent
                }
                FbLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.confirmBody
                    font.pixelSize: Theme.fontMeta
                    color: Theme.textSecondary
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 8
                Item { Layout.fillWidth: true }
                FbButton {
                    id: cancelBtn
                    objectName: root.confirm === "delete" ? "deleteCancel" : "restoreCancel"
                    implicitHeight: 30
                    font.pixelSize: Theme.fontSmall
                    text: qsTr("Cancel")
                    onClicked: root.cancelled()
                }
                Rectangle {
                    id: okBtn
                    objectName: root.confirm === "delete" ? "deleteConfirm" : "restoreConfirm"
                    Layout.preferredHeight: 30
                    Layout.preferredWidth: okText.implicitWidth + 24
                    Layout.minimumWidth: 0
                    radius: Theme.radius6
                    color: root.confirm === "delete" ? Theme.error : Theme.accent
                    opacity: okTap.pressed ? 0.8 : 1
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: root.confirmOk
                    Text { id: okText; anchors.centerIn: parent; text: root.confirmOk; font.pixelSize: Theme.fontSmall; font.weight: Font.DemiBold; color: Theme.textOnAccent }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { id: okTap; onTapped: root.confirmed() }
                    Keys.onPressed: (event) => { if (event.key === Qt.Key_Space || event.key === Qt.Key_Return) { root.confirmed(); event.accepted = true } }
                }
            }
        }
    }
}
