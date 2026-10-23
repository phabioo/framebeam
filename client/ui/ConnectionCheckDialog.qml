import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// 6e Connection check: five steps (Hub reachable, STUN, TURN relay, NAT type, Test connection), a summary card and the
// actions Copy result / Add to problem report / Run again. Modal over the whole window; Esc closes (and cancels a running
// check), R runs again, Ctrl+C copies the result. Opened with player.connectionCheck.show() from Settings.
Item {
    id: root
    required property PlayerController player
    readonly property var check: player.connectionCheck
    readonly property bool shown: check.open

    visible: shown
    anchors.fill: parent
    z: 200

    onShownChanged: if (shown) focusScope.forceActiveFocus()

    function stepIcon(state: string): string {
        switch (state) {
        case "ok": return "✓"
        case "warning": return "!"
        case "failed": return "✕"
        case "skipped": return "–"
        default: return ""
        }
    }
    function stepIconBg(state: string): color {
        switch (state) {
        case "ok": return Theme.ground
        case "warning": return Theme.selected
        case "failed": return Theme.pressed
        default: return "transparent"
        }
    }
    function stepIconFg(state: string): color {
        switch (state) {
        case "ok": return Theme.ok
        case "warning": return Theme.accentText
        case "failed": return Theme.ink2
        case "running": return Theme.accent
        default: return Theme.disabledInk
        }
    }
    function stepRing(state: string): color {
        return state === "running" ? Theme.accent : state === "waiting" || state === "skipped" ? Theme.rule : "transparent"
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.scrim
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: (wheel) => wheel.accepted = true
        onClicked: root.check.close()
    }

    FocusScope {
        id: focusScope
        anchors.fill: parent
        focus: root.shown
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Escape) {
                root.check.close()
                event.accepted = true
            } else if (event.key === Qt.Key_R && !root.check.running && event.modifiers === Qt.NoModifier) {
                root.check.run()
                event.accepted = true
            } else if (event.key === Qt.Key_C && (event.modifiers & Qt.ControlModifier)) {
                root.check.copyResult()
                event.accepted = true
            }
        }

        Rectangle {
            id: card
            objectName: "connectionCheckDialog"
            anchors.centerIn: parent
            width: Math.min(600, parent.width - 32)
            implicitHeight: col.implicitHeight
            height: Math.min(implicitHeight, parent.height - 32)
            radius: 0
            color: Theme.surface
            border.width: 1
            border.color: Theme.rule
            clip: true
            // Swallow clicks inside the card so the scrim click does not close the dialog.
            MouseArea { anchors.fill: parent }

            ColumnLayout {
                id: col
                anchors.fill: parent
                spacing: 0

                // Header
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.topMargin: Theme.space3
                    Layout.bottomMargin: Theme.space2
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 2
                        UzLabel {
                            objectName: "connectionCheckTitle"
                            text: qsTr("Connection check")
                            font.pixelSize: Theme.roles.dialogTitle.size
                            font.weight: Theme.roles.dialogTitle.weight
                            font.letterSpacing: Theme.roles.dialogTitle.spacing
                        }
                        UzLabel {
                            objectName: "connectionCheckSubtitle"
                            Layout.fillWidth: true
                            text: root.check.subtitle
                            color: Theme.muted
                            font.pixelSize: Theme.roles.small.size
                            elide: Text.ElideRight
                        }
                    }
                    UzButton {
                        objectName: "connectionCheckClose"
                        kind: "link"
                        implicitHeight: 30
                        implicitWidth: 30
                        text: "×"
                        Accessible.name: qsTr("Close")
                        onClicked: root.check.close()
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.rule }

                // Steps and summary scroll when the card is taller than the window (large fonts); header and actions stay put.
                Flickable {
                    id: bodyFlick
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: body.implicitHeight
                    Layout.minimumHeight: 0
                    contentWidth: width
                    contentHeight: body.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ColumnLayout {
                        id: body
                        width: bodyFlick.width
                        spacing: 0

                    // Steps
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        Layout.topMargin: 8
                        spacing: 0
                        Repeater {
                            model: root.check.steps
                            delegate: Item {
                                id: row
                                required property var modelData
                                required property int index
                                objectName: "connectionCheckStep_" + modelData.id
                                readonly property string st: modelData.state
                                Layout.fillWidth: true
                                implicitHeight: rowGrid.implicitHeight + 20
                                Accessible.role: Accessible.ListItem
                                Accessible.name: modelData.title + ": " + modelData.result

                                Rectangle {
                                    visible: row.index < 4
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 1
                                    color: Theme.rule2
                                }
                                GridLayout {
                                    id: rowGrid
                                    x: 0
                                    y: 10
                                    width: parent.width
                                    columns: 3
                                    columnSpacing: 12
                                    rowSpacing: 0
                                    Rectangle {
                                        objectName: "connectionCheckIcon_" + row.modelData.id
                                        Layout.preferredWidth: 18
                                        Layout.preferredHeight: 18
                                        Layout.topMargin: 1
                                        Layout.leftMargin: 0
                                        Layout.rightMargin: 4
                                        radius: 0
                                        color: root.stepIconBg(row.st)
                                        border.width: row.st === "running" ? 2 : row.st === "waiting" || row.st === "skipped" ? 1 : 0
                                        border.color: root.stepRing(row.st)
                                        UzLabel {
                                            anchors.centerIn: parent
                                            text: root.stepIcon(row.st)
                                            color: root.stepIconFg(row.st)
                                            font.pixelSize: Theme.roles.meta11.size
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                    UzLabel {
                                        Layout.preferredWidth: 150
                                        Layout.alignment: Qt.AlignTop
                                        text: row.modelData.title
                                        font.pixelSize: Theme.roles.body.size
                                        font.weight: Font.Medium
                                        color: row.st === "waiting" || row.st === "skipped" ? Theme.dim : Theme.ink
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        spacing: 2
                                        UzLabel {
                                            objectName: "connectionCheckResult_" + row.modelData.id
                                            Layout.fillWidth: true
                                            text: row.modelData.result
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: Theme.roles.input.size
                                            color: row.st === "warning" ? Theme.accentText
                                                 : row.st === "waiting" || row.st === "skipped" ? Theme.dim : Theme.ink2
                                        }
                                        UzLabel {
                                            objectName: "connectionCheckExplain_" + row.modelData.id
                                            visible: text !== ""
                                            Layout.fillWidth: true
                                            text: row.modelData.explanation
                                            wrapMode: Text.WordWrap
                                            lineHeight: 17
                                            lineHeightMode: Text.FixedHeight
                                            font.pixelSize: Theme.roles.small.size
                                            color: Theme.muted
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Summary card
                    Rectangle {
                        id: summaryCard
                        objectName: "connectionCheckSummary"
                        visible: root.check.summary !== "" && !root.check.running
                        readonly property string tone: root.check.summaryTone
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        Layout.topMargin: 8
                        implicitHeight: sumCol.implicitHeight + 2 * Theme.space15
                        radius: 0
                        color: tone === "ok" ? Theme.ground : tone === "warn" ? Theme.selected : Theme.surface
                        border.width: 1
                        border.color: tone === "ok" ? Theme.ok : tone === "warn" ? Theme.accent : Theme.rule
                        ColumnLayout {
                            id: sumCol
                            anchors.fill: parent
                            anchors.margins: Theme.space15
                            anchors.leftMargin: Theme.space2
                            anchors.rightMargin: Theme.space2
                            spacing: 3
                            UzLabel {
                                objectName: "connectionCheckSummaryTitle"
                                Layout.fillWidth: true
                                text: root.check.summary
                                wrapMode: Text.WordWrap
                                font.pixelSize: Theme.roles.body.size
                                font.weight: Font.Medium
                                color: summaryCard.tone === "ok" ? Theme.ok : summaryCard.tone === "warn" ? Theme.accentText : Theme.ink
                            }
                            UzLabel {
                                objectName: "connectionCheckSummaryDetail"
                                visible: text !== ""
                                Layout.fillWidth: true
                                text: root.check.summaryDetail
                                wrapMode: Text.WordWrap
                                lineHeight: 18
                                lineHeightMode: Text.FixedHeight
                                font.pixelSize: Theme.roles.small.size
                                color: Theme.ink2
                            }
                        }
                    }
                    }
                }

                // Actions while running: progress and Cancel
                RowLayout {
                    visible: root.check.running
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.topMargin: Theme.space2
                    Layout.bottomMargin: Theme.space3
                    spacing: 8
                    UzLabel {
                        objectName: "connectionCheckProgress"
                        text: root.check.progressText
                        color: Theme.accentText
                        font.pixelSize: Theme.roles.input.size
                    }
                    Item { Layout.fillWidth: true }
                    UzButton {
                        objectName: "connectionCheckCancel"
                                                text: qsTr("Cancel")
                        onClicked: root.check.cancel()
                    }
                }

                // Actions of a finished check. A Flow: with wide fonts (Windows) the buttons wrap to a second line instead of
                // leaving the 600 px card.
                Flow {
                    id: actionFlow
                    objectName: "connectionCheckActions"
                    visible: !root.check.running
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.topMargin: Theme.space2
                    Layout.bottomMargin: Theme.space3
                    Layout.preferredHeight: implicitHeight
                    spacing: 8

                    UzButton {
                        objectName: "connectionCheckCopy"
                                                text: root.check.copied ? qsTr("Copied") : qsTr("Copy result")
                        onClicked: root.check.copyResult()
                    }
                    UzButton {
                        objectName: "connectionCheckAttach"
                        enabled: root.check.canAttach && !root.check.attached
                                                text: root.check.attached ? qsTr("Added to the next report") : qsTr("Add to problem report")
                        onClicked: root.check.addToReport()
                    }
                    UzButton {
                        objectName: "connectionCheckEditHub"
                        visible: root.check.canEditHubAddress
                                                text: qsTr("Edit server address…")
                        onClicked: root.check.editHubAddress()
                    }
                    UzButton {
                        objectName: "connectionCheckRun"
                                                kind: root.check.canEditHubAddress ? "primary" : "outline"
                        text: qsTr("Run again")
                        onClicked: root.check.run()
                    }
                }
            }
        }
    }
}
