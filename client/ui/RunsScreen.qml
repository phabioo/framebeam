import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Runs (docs/design/plugins.md 4j-2): invite cards, runs by group (Active / Paused, Finished), one run in detail with its
// participants, status actions and the event log. List, invite and log belong to FrameBeam; plugins only build on them.
Rectangle {
    id: root
    objectName: "runsScreen"
    required property PlayerController player
    readonly property RunsController runs: root.player.runs
    readonly property bool inDetail: root.runs.openRunId !== ""
    property string confirmAction: ""      // finished | abandoned | leave
    color: Theme.bg

    onVisibleChanged: if (!visible && root.runs) root.runs.closeRun()
    function ask(action: string) {
        root.confirmAction = action
        confirmDialog.open()
    }

    component RunRowDelegate: Rectangle {
        id: row
        required property var model
        objectName: "runRow_" + row.model.runId
        Layout.fillWidth: true
        implicitHeight: rowLayout.implicitHeight + 24
        radius: Theme.radius10
        color: rowHover.hovered ? Theme.rowHover : Theme.surface
        border.width: 1
        border.color: Theme.borderCard
        Accessible.role: Accessible.Button
        Accessible.name: row.model.name
        RowLayout {
            id: rowLayout
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: Theme.space12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space8
                    FbLabel {
                        objectName: "runRowName"
                        Layout.fillWidth: true
                        text: row.model.name
                        elide: Text.ElideRight
                        font.weight: Font.DemiBold
                        font.pixelSize: Theme.fontSection
                    }
                    FbPill { objectName: "runRowStatus"; tone: row.model.tone; text: row.model.statusText }
                }
                FbLabel {
                    Layout.fillWidth: true
                    text: row.model.subText
                    elide: Text.ElideRight
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontMeta
                }
            }
            RunAvatars { participants: row.model.participants; Layout.alignment: Qt.AlignVCenter }
            FbMono {
                Layout.alignment: Qt.AlignVCenter
                text: row.model.timeText
                color: Theme.textMeta
                font.pixelSize: Theme.fontMono
                visible: root.width > 760
            }
            FbLabel {
                Layout.alignment: Qt.AlignVCenter
                text: row.model.action
                color: Theme.accent
                font.weight: Font.Medium
                font.pixelSize: Theme.fontSmall
            }
        }
        HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.runs.openRun(row.model.runId) }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.sidebarWidth
            player: root.player
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: page.implicitHeight + 56
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: page
                x: Theme.space28
                y: Theme.space28
                width: Math.min(flick.width - 2 * Theme.space28, Theme.contentMaxWidth)
                spacing: Theme.space16

                // ---------------------------------------------------------------- shared: message banner
                Rectangle {
                    objectName: "runsMessage"
                    visible: root.runs.message !== ""
                    Layout.fillWidth: true
                    implicitHeight: msgRow.implicitHeight + 20
                    radius: Theme.radius8
                    color: root.runs.messageIsError ? Theme.errorBg : Theme.infoBg
                    border.width: 1
                    border.color: root.runs.messageIsError ? Theme.dangerBorder : Theme.infoBorder
                    RowLayout {
                        id: msgRow
                        anchors.fill: parent
                        anchors.margins: 10
                        FbLabel {
                            Layout.fillWidth: true
                            text: root.runs.message
                            wrapMode: Text.WordWrap
                            color: root.runs.messageIsError ? Theme.errorText : Theme.infoText
                            font.pixelSize: Theme.fontSmall
                        }
                        FbButton { kind: "link"; text: qsTr("Dismiss"); onClicked: root.runs.dismissMessage() }
                    }
                }
                RowLayout {
                    visible: root.runs.busy
                    spacing: Theme.space8
                    FbSpinner { size: 14; color: Theme.accent }
                    FbLabel { objectName: "runsBusy"; text: root.runs.busyText; color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                }

                // ================================================================ list
                ColumnLayout {
                    visible: !root.inDetail
                    Layout.fillWidth: true
                    spacing: Theme.space16

                    RowLayout {
                        Layout.fillWidth: true
                        FbLabel {
                            Layout.fillWidth: true
                            text: qsTr("Runs")
                            font.pixelSize: Theme.fontPage
                            font.weight: Font.DemiBold
                        }
                        FbButton {
                            objectName: "runsStartButton"
                            kind: "primary"
                            text: qsTr("Start a run")
                            onClicked: startDialog.openStart()
                        }
                    }

                    Repeater {
                        model: root.runs.invites
                        delegate: Rectangle {
                            id: card
                            required property var model
                            objectName: "runInvite_" + card.model.runId
                            Layout.fillWidth: true
                            implicitHeight: cardCol.implicitHeight + 32
                            radius: Theme.radius10
                            color: Theme.accentChipBg
                            border.width: 1
                            border.color: Theme.accentBoxBorder
                            ColumnLayout {
                                id: cardCol
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 6
                                Eyebrow { text: qsTr("Run invite"); color: Theme.accent }
                                FbLabel {
                                    objectName: "runInviteHeadline"
                                    Layout.fillWidth: true
                                    text: card.model.headline
                                    wrapMode: Text.WordWrap
                                    font.weight: Font.DemiBold
                                    font.pixelSize: Theme.fontSection
                                }
                                FbLabel {
                                    Layout.fillWidth: true
                                    text: card.model.subText + " · " + card.model.joinedText
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontSmall
                                    wrapMode: Text.WordWrap
                                }
                                RowLayout {
                                    Layout.topMargin: 4
                                    spacing: Theme.space8
                                    FbButton {
                                        objectName: "runInviteDecline"
                                        text: qsTr("Decline")
                                        enabled: !root.runs.busy
                                        onClicked: root.runs.declineRun(card.model.runId)
                                    }
                                    FbButton {
                                        objectName: "runInviteJoin"
                                        kind: "primary"
                                        text: qsTr("Join")
                                        enabled: !root.runs.busy
                                        onClicked: joinDialog.openFor(card.model.runId, card.model.name, card.model.inviteLine)
                                    }
                                }
                            }
                        }
                    }

                    Eyebrow {
                        objectName: "runsActiveTitle"
                        visible: root.runs.active.count > 0
                        text: root.runs.activeTitle
                    }
                    Repeater {
                        model: root.runs.active
                        delegate: RunRowDelegate {}
                    }

                    Eyebrow {
                        objectName: "runsFinishedTitle"
                        visible: root.runs.finished.count > 0
                        text: qsTr("Finished · %1").arg(root.runs.finished.count)
                    }
                    Repeater {
                        model: root.runs.finished
                        delegate: RunRowDelegate {}
                    }

                    FbLabel {
                        objectName: "runsEmpty"
                        visible: root.runs.loaded && root.runs.invites.count === 0 && root.runs.active.count === 0 && root.runs.finished.count === 0
                        Layout.fillWidth: true
                        text: qsTr("No runs yet. Start one with the plugins of your Hub, or wait for an invite.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                    }
                }

                // ================================================================ detail
                ColumnLayout {
                    id: detailCol
                    visible: root.inDetail
                    Layout.fillWidth: true
                    spacing: Theme.space16
                    readonly property var d: root.runs.detail
                    // The run actions of the "⋯" menu. Rebuilt only when their content changes, so the menu is not reset on
                    // every update of the run.
                    property var menuItems: []
                    property string menuJson: ""
                    function rebuildMenu() {
                        const d = detailCol.d
                        const out = []
                        if (d.canStart === true) out.push({ name: "start", label: qsTr("Start run now") })
                        if (d.canPause === true) out.push({ name: "pause", label: qsTr("Pause run") })
                        if (d.canResume === true) out.push({ name: "resume", label: qsTr("Resume run") })
                        if (d.canLeave === true) out.push({ name: "leave", label: qsTr("Leave run…"), danger: true, divider: out.length > 0 })
                        if (d.canFinish === true) out.push({ name: "finish", label: qsTr("Finish run…") })
                        if (d.canAbandon === true) out.push({ name: "abandon", label: qsTr("Abandon run…"), danger: true, divider: true })
                        const j = JSON.stringify(out)
                        if (j === detailCol.menuJson) return
                        detailCol.menuJson = j
                        detailCol.menuItems = out
                    }
                    onDChanged: rebuildMenu()
                    Component.onCompleted: rebuildMenu()

                    FbButton {
                        objectName: "runsBack"
                        kind: "link"
                        text: qsTr("‹ Runs")
                        onClicked: root.runs.closeRun()
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space12
                        FbLabel {
                            objectName: "runDetailName"
                            Layout.fillWidth: true
                            text: detailCol.d.name || ""
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontDetail
                            font.weight: Font.DemiBold
                        }
                        FbPill { objectName: "runDetailStatus"; tone: detailCol.d.tone || "neutral"; text: detailCol.d.statusText || "" }
                    }
                    FbLabel {
                        objectName: "runDetailLine"
                        Layout.fillWidth: true
                        text: detailCol.d.line || ""
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                    }
                    FbLabel {
                        Layout.fillWidth: true
                        text: detailCol.d.pluginsText || ""
                        wrapMode: Text.WordWrap
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                    FbLabel {
                        visible: (detailCol.d.note || "") !== ""
                        Layout.fillWidth: true
                        text: detailCol.d.note || ""
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                    }

                    // One primary (Play), Invite as the secondary, the state actions in the "⋯" menu (P24, 4j-3).
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space8
                        FbButton {
                            objectName: "runPlay"
                            visible: detailCol.d.canPlay === true
                            kind: "primary"
                            text: detailCol.d.playLabel || qsTr("Play run")
                            enabled: !root.runs.busy
                            onClicked: root.runs.playRun(detailCol.d.runId)
                        }
                        FbButton {
                            objectName: "runInvitePlayers"
                            visible: detailCol.d.canInvite === true
                            text: qsTr("Invite players")
                            onClicked: startDialog.openInvite(detailCol.d.runId)
                        }
                        Item {
                            Layout.preferredWidth: 30
                            Layout.preferredHeight: 30
                            visible: detailCol.menuItems.length > 0
                            MoreMenu {
                                objectName: "runMenu"
                                anchors.fill: parent
                                enabled: !root.runs.busy
                                tooltip: qsTr("More actions for this run")
                                items: detailCol.menuItems
                                onActivated: name => {
                                    const id = detailCol.d.runId
                                    if (name === "start" || name === "resume") root.runs.setRunStatus(id, "active")
                                    else if (name === "pause") root.runs.setRunStatus(id, "paused")
                                    else if (name === "finish") root.ask("finished")
                                    else if (name === "abandon") root.ask("abandoned")
                                    else if (name === "leave") root.ask("leave")
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Eyebrow { text: qsTr("Players · %1").arg(root.runs.participants.count) }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: partCol.implicitHeight
                        radius: Theme.radius10
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.borderCard
                        ColumnLayout {
                            id: partCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: 0
                            Repeater {
                                model: root.runs.participants
                                delegate: Item {
                                    id: prow
                                    required property var model
                                    required property int index
                                    objectName: "runParticipant_" + prow.model.userId
                                    Layout.fillWidth: true
                                    implicitHeight: 48
                                    Rectangle {
                                        visible: prow.index > 0
                                        anchors.top: parent.top
                                        width: parent.width
                                        height: 1
                                        color: Theme.borderRow
                                    }
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 16
                                        anchors.rightMargin: 16
                                        spacing: Theme.space12
                                        RunAvatars {
                                            participants: [{ name: prow.model.name, color: prow.model.color }]
                                            size: 28
                                        }
                                        FbLabel {
                                            Layout.fillWidth: true
                                            text: prow.model.me ? qsTr("%1 (you)").arg(prow.model.name) : prow.model.name
                                            elide: Text.ElideRight
                                            font.weight: Font.Medium
                                        }
                                        FbLabel {
                                            Layout.preferredWidth: 200
                                            visible: root.width > 760
                                            text: prow.model.gameTitle
                                            elide: Text.ElideRight
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.fontSmall
                                        }
                                        FbLabel {
                                            text: prow.model.statusText
                                            color: prow.model.statusText === qsTr("Joined") ? Theme.ok : Theme.textMuted
                                            font.pixelSize: Theme.fontSmall
                                        }
                                    }
                                }
                            }
                        }
                    }

                    PluginRunSection { Layout.fillWidth: true; player: root.player }

                    Eyebrow { objectName: "runEventsTitle"; text: qsTr("Event log · %1").arg(root.runs.events.count) }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Math.max(48, evCol.implicitHeight)
                        radius: Theme.radius10
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.borderCard
                        FbLabel {
                            anchors.centerIn: parent
                            visible: root.runs.events.count === 0
                            text: qsTr("No events yet.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                        }
                        ColumnLayout {
                            id: evCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: 0
                            Repeater {
                                model: root.runs.events
                                delegate: Item {
                                    id: erow
                                    required property var model
                                    required property int index
                                    objectName: "runEvent_" + erow.model.seq
                                    Layout.fillWidth: true
                                    implicitHeight: evLayout.implicitHeight + 16
                                    Rectangle {
                                        visible: erow.index > 0
                                        anchors.top: parent.top
                                        width: parent.width
                                        height: 1
                                        color: Theme.borderRow
                                    }
                                    RowLayout {
                                        id: evLayout
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.leftMargin: 16
                                        anchors.rightMargin: 16
                                        spacing: Theme.space12
                                        FbMono {
                                            objectName: "runEventTime"
                                            Layout.preferredWidth: 130
                                            text: erow.model.timeText
                                            color: Theme.textMeta
                                            font.pixelSize: Theme.fontMono
                                            elide: Text.ElideRight
                                            HoverHandler { id: evHover }
                                            ToolTip.visible: evHover.hovered && erow.model.fullTimeText !== ""
                                            ToolTip.text: erow.model.fullTimeText
                                        }
                                        FbLabel {
                                            Layout.fillWidth: true
                                            text: erow.model.text
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: Theme.fontSmall
                                        }
                                        FbLabel {
                                            Layout.preferredWidth: 170
                                            visible: root.width > 820
                                            text: erow.model.sourceText
                                            elide: Text.ElideRight
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.fontMeta
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    RunJoinDialog {
        id: joinDialog
        runs: root.runs
    }
    RunStartDialog {
        id: startDialog
        runs: root.runs
    }

    // Finish / abandon / leave ask first.
    Popup {
        id: confirmDialog
        objectName: "runConfirmDialog"
        parent: Window.window ? Window.window.contentItem : null
        anchors.centerIn: parent
        modal: true
        dim: true
        focus: true
        width: Math.min(420, parent ? parent.width - 32 : 420)
        padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: Theme.radius12
            color: Theme.popupBg
            border.width: 1
            border.color: Theme.borderPopup
        }
        contentItem: ColumnLayout {
            spacing: Theme.space12
            FbLabel {
                objectName: "runConfirmTitle"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontCard
                font.weight: Font.DemiBold
                text: root.confirmAction === "finished" ? qsTr("Finish this run?")
                      : (root.confirmAction === "abandoned" ? qsTr("Abandon this run?") : qsTr("Leave this run?"))
            }
            FbLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                text: root.confirmAction === "leave" ? qsTr("You stop taking part. The others continue; your events stay in the log.")
                      : qsTr("It ends for everyone. No new events are recorded afterwards and the run cannot be resumed.")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space8
                Item { Layout.fillWidth: true }
                FbButton { objectName: "runConfirmCancel"; text: qsTr("Cancel"); onClicked: confirmDialog.close() }
                FbButton {
                    objectName: "runConfirmOk"
                    kind: "primary"
                    text: root.confirmAction === "finished" ? qsTr("Finish run") : (root.confirmAction === "abandoned" ? qsTr("Abandon run") : qsTr("Leave run"))
                    onClicked: {
                        const action = root.confirmAction
                        const id = root.runs.openRunId
                        root.confirmAction = ""
                        confirmDialog.close()
                        if (action === "leave") root.runs.leaveRun(id)
                        else root.runs.setRunStatus(id, action)
                    }
                }
            }
        }
    }
}
