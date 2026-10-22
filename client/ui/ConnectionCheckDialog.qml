import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

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
        case "ok": return Theme.okBg
        case "warning": return Theme.pluginWarnBg
        case "failed": return Theme.neutralPillBg
        default: return "transparent"
        }
    }
    function stepIconFg(state: string): color {
        switch (state) {
        case "ok": return Theme.ok
        case "warning": return Theme.pluginWarn
        case "failed": return Theme.textSecondary
        case "running": return Theme.accent
        default: return Theme.textDisabled
        }
    }
    function stepRing(state: string): color {
        return state === "running" ? Theme.accent : state === "waiting" || state === "skipped" ? Theme.borderButton : "transparent"
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.dark ? "#b3050506" : "#99000000"
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
            radius: 14
            color: Theme.popupBg
            border.width: 1
            border.color: Theme.borderPopup
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
                    Layout.topMargin: 22
                    Layout.bottomMargin: 14
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 2
                        FbLabel {
                            objectName: "connectionCheckTitle"
                            text: qsTr("Connection check")
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                        }
                        FbLabel {
                            objectName: "connectionCheckSubtitle"
                            Layout.fillWidth: true
                            text: root.check.subtitle
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontMeta
                            elide: Text.ElideRight
                        }
                    }
                    FbButton {
                        objectName: "connectionCheckClose"
                        kind: "link"
                        implicitHeight: 30
                        implicitWidth: 30
                        text: "×"
                        Accessible.name: qsTr("Close")
                        onClicked: root.check.close()
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderInput }

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
                                color: Theme.borderRow
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
                                    radius: 9
                                    color: root.stepIconBg(row.st)
                                    border.width: row.st === "running" ? 2 : row.st === "waiting" || row.st === "skipped" ? 1.5 : 0
                                    border.color: root.stepRing(row.st)
                                    FbLabel {
                                        anchors.centerIn: parent
                                        text: root.stepIcon(row.st)
                                        color: root.stepIconFg(row.st)
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                }
                                FbLabel {
                                    Layout.preferredWidth: 150
                                    Layout.alignment: Qt.AlignTop
                                    text: row.modelData.title
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.Medium
                                    color: row.st === "waiting" || row.st === "skipped" ? Theme.textFaint : Theme.text
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 2
                                    FbLabel {
                                        objectName: "connectionCheckResult_" + row.modelData.id
                                        Layout.fillWidth: true
                                        text: row.modelData.result
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: Theme.fontSmall
                                        color: row.st === "warning" ? Theme.pluginWarn
                                             : row.st === "waiting" || row.st === "skipped" ? Theme.textFaint : Theme.textSecondary
                                    }
                                    FbLabel {
                                        objectName: "connectionCheckExplain_" + row.modelData.id
                                        visible: text !== ""
                                        Layout.fillWidth: true
                                        text: row.modelData.explanation
                                        wrapMode: Text.WordWrap
                                        lineHeight: 17
                                        lineHeightMode: Text.FixedHeight
                                        font.pixelSize: Theme.fontMeta
                                        color: Theme.textMeta
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
                    implicitHeight: sumCol.implicitHeight + 24
                    radius: 8
                    color: tone === "ok" ? Theme.okBg : tone === "warn" ? Theme.amberRow : Theme.surface
                    border.width: 1
                    border.color: tone === "ok" ? (Theme.dark ? "#1f3a2a" : "#bfe0cb") : tone === "warn" ? Theme.pluginWarnLine : Theme.borderInput
                    ColumnLayout {
                        id: sumCol
                        anchors.fill: parent
                        anchors.margins: 12
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 3
                        FbLabel {
                            objectName: "connectionCheckSummaryTitle"
                            Layout.fillWidth: true
                            text: root.check.summary
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                            color: summaryCard.tone === "ok" ? Theme.ok : summaryCard.tone === "warn" ? Theme.pluginWarn : Theme.text
                        }
                        FbLabel {
                            objectName: "connectionCheckSummaryDetail"
                            visible: text !== ""
                            Layout.fillWidth: true
                            text: root.check.summaryDetail
                            wrapMode: Text.WordWrap
                            lineHeight: 18
                            lineHeightMode: Text.FixedHeight
                            font.pixelSize: Theme.fontMeta
                            color: Theme.textSecondary
                        }
                    }
                }

                // Actions
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.topMargin: 16
                    Layout.bottomMargin: 20
                    spacing: 8

                    FbLabel {
                        objectName: "connectionCheckProgress"
                        visible: root.check.running
                        text: root.check.progressText
                        color: Theme.accent
                        font.pixelSize: Theme.fontSmall
                    }
                    Item { visible: root.check.running; Layout.fillWidth: true }
                    FbButton {
                        objectName: "connectionCheckCancel"
                        visible: root.check.running
                        implicitHeight: 34
                        text: qsTr("Cancel")
                        onClicked: root.check.cancel()
                    }

                    FbButton {
                        objectName: "connectionCheckCopy"
                        visible: !root.check.running
                        implicitHeight: 34
                        text: root.check.copied ? qsTr("Copied") : qsTr("Copy result")
                        onClicked: root.check.copyResult()
                    }
                    FbButton {
                        objectName: "connectionCheckAttach"
                        visible: !root.check.running
                        enabled: root.check.canAttach && !root.check.attached
                        implicitHeight: 34
                        text: root.check.attached ? qsTr("Added to the next report") : qsTr("Add to problem report")
                        onClicked: root.check.addToReport()
                    }
                    Item { visible: !root.check.running; Layout.fillWidth: true }
                    FbButton {
                        objectName: "connectionCheckEditHub"
                        visible: root.check.canEditHubAddress
                        implicitHeight: 34
                        text: qsTr("Edit Hub address…")
                        onClicked: root.check.editHubAddress()
                    }
                    FbButton {
                        objectName: "connectionCheckRun"
                        visible: !root.check.running
                        implicitHeight: 34
                        kind: root.check.canEditHubAddress ? "primary" : "outline"
                        text: qsTr("Run again")
                        onClicked: root.check.run()
                    }
                }
            }
        }
    }
}
