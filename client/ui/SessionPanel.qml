import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// 3g: side panel (340) of the game view: visibility, invited/watching with connection pills and relay hint, invite
// search, save slot with snapshot, Stop sharing, Quit game and save, diagnostics toggle.
Rectangle {
    id: root
    required property PlayerController player
    readonly property SessionController ctl: player.sessions
    readonly property SaveHistoryController saves: player.saveHistory
    readonly property DiagnosticsModel diag: ctl.diagnostics
    objectName: "sessionPanel"
    color: Theme.gameHeader
    implicitWidth: 340

    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.gameBorder }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 20
        anchors.bottomMargin: 20
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        spacing: 14

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: col.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            // A plain Column, not a ColumnLayout: a layout sizes its single cell to the widest minimum of any child, so one
            // item that cannot shrink (platform fonts, translations) would push every block and its buttons past the
            // panel (seen with Qt 6.8 on Windows). Here every block gets exactly the panel width.
            Column {
                id: col
                width: flick.width
                spacing: 14

                Rectangle {
                    objectName: "panelHubBanner"
                    width: col.width
                    visible: root.ctl.hubLink !== "online"
                    implicitHeight: bannerText.implicitHeight + 16
                    radius: 8
                    color: Theme.accentChipBg
                    FbLabel {
                        id: bannerText
                        anchors.fill: parent
                        anchors.margins: 8
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: Theme.accent
                        text: root.ctl.shared ? qsTr("Hub not reachable · reconnecting. The running Session continues until the Hub ends it.")
                                              : qsTr("Hub not reachable · reconnecting. Sharing needs a Hub connection.")
                    }
                }

                Rectangle {
                    objectName: "panelMessage"
                    width: col.width
                    visible: root.ctl.message !== ""
                    implicitHeight: msgText.implicitHeight + 16
                    radius: 8
                    color: root.ctl.messageIsError ? Theme.errorBg : Theme.surfaceRaised
                    FbLabel {
                        id: msgText
                        anchors.fill: parent
                        anchors.margins: 8
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: root.ctl.messageIsError ? Theme.errorText : Theme.gameText
                        text: root.ctl.message
                    }
                }

                // Blocks are plain Columns whose children take exactly the block width (see the note on `col`).
                Column {
                    id: visBlock
                    width: col.width
                    spacing: 8
                    Eyebrow { width: parent.width; elide: Text.ElideRight; text: qsTr("Visibility") }
                    FbSegment {
                        objectName: "visibilitySegment"
                        width: parent.width
                        stretch: true
                        current: root.ctl.visibility
                        options: [
                            { value: "private", label: qsTr("Private"), name: "visPrivate" },
                            { value: "hub_users", label: qsTr("Hub users"), name: "visHubUsers" },
                            { value: "invite_only", label: qsTr("Invite only"), name: "visInviteOnly" }
                        ]
                        onPicked: (v) => root.ctl.setVisibility(v)
                    }
                    FbLabel {
                        objectName: "visibilityHint"
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: !root.ctl.shared ? qsTr("Not shared yet. Choose who may watch, then share the Session.")
                                               : qsTr("Your own devices can always watch.")
                        font.pixelSize: 11
                        color: Theme.gameTextMuted
                    }
                }

                // Invited / watching (only while shared)
                Column {
                    id: partBlock
                    objectName: "participantsBlock"
                    width: col.width
                    visible: root.ctl.shared
                    spacing: 8
                    RowLayout {
                        width: parent.width
                        Eyebrow { objectName: "participantsTitle"; Layout.minimumWidth: 0; elide: Text.ElideRight; text: root.ctl.participantsTitle }
                        FbLabel {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                            text: qsTr("only you can change this")
                            font.pixelSize: 11
                            color: Theme.gameTextMuted
                        }
                    }
                    FbLabel {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        visible: root.ctl.participants.length === 0
                        text: root.ctl.visibility === "invite_only" ? qsTr("Nobody invited yet.") : qsTr("Nobody is watching yet.")
                        font.pixelSize: 12
                        color: Theme.gameTextMuted
                    }
                    Repeater {
                        model: root.ctl.participants
                        // Item with anchors instead of a RowLayout: the action button is pinned to the right edge of the
                        // block, the text takes the rest and elides, so the button cannot leave the panel.
                        delegate: Item {
                            id: prow
                            required property var modelData
                            objectName: "participant_" + modelData.id
                            width: partBlock.width
                            implicitHeight: Math.max(30, ptext.implicitHeight)
                            StatusDot {
                                id: pdot
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                tone: prow.modelData.online ? "ok" : "neutral"
                            }
                            FbButton {
                                id: pbtn
                                objectName: (prow.modelData.kind === "viewer" ? "remove_" : "withdraw_") + prow.modelData.id
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                implicitHeight: 30
                                implicitWidth: Math.max(60, implicitContentWidth + 20)
                                font.pixelSize: 12
                                focusPolicy: Qt.NoFocus
                                text: prow.modelData.action
                                onClicked: prow.modelData.kind === "viewer" ? root.ctl.removeViewer(prow.modelData.id)
                                                                           : root.ctl.withdrawInvite(prow.modelData.id)
                            }
                            FbPill {
                                id: ppill
                                objectName: "participantPill"
                                anchors.right: pbtn.left
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                visible: prow.modelData.kind === "viewer" && !!prow.modelData.connection
                                small: true
                                text: prow.modelData.connection || ""
                                tone: prow.modelData.connectionTone || "neutral"
                            }
                            ColumnLayout {
                                id: ptext
                                anchors.left: pdot.right
                                anchors.leftMargin: 8
                                anchors.right: ppill.visible ? ppill.left : pbtn.left
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 0
                                FbLabel { Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight; text: prow.modelData.name; font.pixelSize: 14; color: Theme.gameText }
                                FbLabel { Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight; objectName: "participantStatus"; text: prow.modelData.status; font.pixelSize: 12; color: Theme.gameTextMuted }
                            }
                        }
                    }
                    // Relay hint (D9)
                    Rectangle {
                        objectName: "relayHint"
                        width: parent.width
                        visible: root.ctl.relayHint !== ""
                        implicitHeight: relayRow.implicitHeight + 18
                        radius: 8
                        color: Theme.infoBg
                        border.width: 1
                        border.color: Theme.infoBorder
                        RowLayout {
                            id: relayRow
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 8
                            FbLabel { text: "ⓘ"; font.pixelSize: 12; color: Theme.accent; Layout.alignment: Qt.AlignTop }
                            FbLabel {
                                objectName: "relayHintText"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: root.ctl.relayHint
                                wrapMode: Text.WordWrap
                                font.pixelSize: 12
                                color: Theme.infoText
                            }
                        }
                    }
                }

                // Invite search (invite_only)
                Column {
                    id: invBlock
                    objectName: "inviteBlock"
                    width: col.width
                    visible: root.ctl.visibility === "invite_only"
                    spacing: 8
                    RowLayout {
                        width: parent.width
                        FbLabel { Layout.minimumWidth: 0; elide: Text.ElideRight; text: qsTr("Invite"); font.pixelSize: 12; color: Theme.gameTextMuted }
                        FbLabel {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                            text: qsTr("Users on this hub")
                            font.pixelSize: 11
                            color: Theme.gameTextMuted
                        }
                    }
                    FbField {
                        id: inviteField
                        objectName: "inviteField"
                        width: parent.width
                        implicitHeight: 36
                        enabled: root.ctl.shared
                        font.family: Qt.application.font.family
                        placeholderText: root.ctl.shared ? qsTr("Search users…") : qsTr("Share the Session to invite users")
                        onTextChanged: root.ctl.searchUsers(text)
                    }
                    FbLabel {
                        objectName: "userSearchHint"
                        width: parent.width
                        visible: root.ctl.userSearchHint.length > 0
                        wrapMode: Text.WordWrap
                        text: root.ctl.userSearchHint
                        font.pixelSize: 12
                        color: Theme.gameTextMuted
                    }
                    Repeater {
                        model: root.ctl.userResults
                        delegate: RowLayout {
                            id: urow
                            required property var modelData
                            objectName: "userResult_" + modelData.id
                            width: invBlock.width
                            spacing: 10
                            StatusDot { tone: urow.modelData.online ? "ok" : "neutral" }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.preferredWidth: 0
                                spacing: 0
                                FbLabel { Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight; text: urow.modelData.name; font.pixelSize: 14; color: Theme.gameText }
                                FbLabel {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: urow.modelData.hint
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 11
                                    color: Theme.gameTextMuted
                                }
                            }
                            FbButton {
                                objectName: "invite_" + urow.modelData.id
                                implicitHeight: 30
                                kind: "primary"
                                font.pixelSize: 12
                                focusPolicy: Qt.NoFocus
                                text: qsTr("Invite")
                                onClicked: { root.ctl.invite(urow.modelData.id); inviteField.text = "" }
                            }
                        }
                    }
                }

                FbLabel {
                    objectName: "readOnlyNote"
                    width: col.width
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: Theme.gameTextMuted
                    text: qsTr("Invitees can only watch and listen. They send no input and cannot invite others.")
                }
            }
        }

        // SPEED-UP: speed of the running game only (not persisted); the toggle is in the header (Space).
        Column {
            id: speedBlock
            objectName: "speedBlock"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            visible: root.player.gameSession.active && root.player.gameSession.fastForwardAvailable
            spacing: 8
            Eyebrow { width: parent.width; elide: Text.ElideRight; text: qsTr("Speed-up speed") }
            FbSelect {
                objectName: "speedSelect"
                width: parent.width
                focusPolicy: Qt.NoFocus
                model: root.player.gameSession.speedUpRatios.map(function (r) { return { value: String(r), label: r + "×" } })
                current: String(root.player.gameSession.fastForwardRatio)
                onPicked: (v) => root.player.gameSession.fastForwardRatio = Number(v)
            }
        }

        // SAVE SLOT: the slot of the running game (it cannot change while the game runs) and a manual snapshot
        // Plain Column: children take exactly the block width, nothing can demand more (see the note on `col`).
        Column {
            id: slotBlock
            objectName: "saveSlotBlock"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            visible: root.player.gameSession.active && root.saves.slotsAvailable
            spacing: 8
            RowLayout {
                width: parent.width
                Eyebrow { Layout.minimumWidth: 0; elide: Text.ElideRight; text: qsTr("Save slot") }
                FbLabel {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    text: qsTr("synced to hub")
                    font.pixelSize: 11
                    color: Theme.gameTextMuted
                }
            }
            RowLayout {
                width: parent.width
                spacing: 8
                FbSegment {
                    objectName: "slotSegment"
                    Layout.fillWidth: true
                    stretch: true
                    current: root.saves.slot
                    options: root.saves.slotOptions.map(function (o) {
                        return { value: o.value, label: o.label, name: "slot_" + o.value, disabled: o.value !== root.saves.slot }
                    })
                }
                FbButton {
                    objectName: "newSlotButton"
                    implicitWidth: 38
                    implicitHeight: 36
                    focusPolicy: Qt.NoFocus
                    text: "+"
                    enabled: false   // slots change between games: quit the game first (SaveHistoryController)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Quit the game to add or change a slot")
                }
            }
            KeyHintButton {
                objectName: "gameSnapshotButton"
                width: parent.width
                implicitHeight: 38
                focusPolicy: Qt.NoFocus
                kind: "raised"
                text: root.saves.busy ? qsTr("Saving snapshot…") : qsTr("Save snapshot")
                hint: root.player.controllers.hotkeyLabels.snapshot
                enabled: root.saves.available && !root.saves.busy
                onClicked: root.saves.createSnapshotInGame("")
            }
            FbLabel {
                objectName: "gameSnapshotMessage"
                width: parent.width
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                text: root.saves.message !== "" ? root.saves.message
                      : qsTr("Saved to “%1” · final sync on pause or exit").arg(root.saves.slot === "default" ? qsTr("default") : root.saves.slot)
                color: root.saves.message === "" ? Theme.gameTextMuted : (root.saves.messageIsError ? Theme.error : Theme.ok)
            }
        }

        Column {
            id: btnBlock
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            spacing: 8
            FbButton {
                objectName: "shareButton"
                width: parent.width
                implicitHeight: 42
                focusPolicy: Qt.NoFocus
                kind: root.ctl.shared ? "outline" : "primary"
                text: root.ctl.shared ? qsTr("Stop sharing") : (root.ctl.shareBusy ? qsTr("Sharing…") : qsTr("Share Session"))
                enabled: root.ctl.shared || (!root.ctl.shareBusy && root.ctl.available && root.ctl.hubLink === "online")
                onClicked: root.ctl.shared ? root.ctl.stopSharing() : root.ctl.shareSession()
            }
            FbButton {
                objectName: "endGameButton"
                width: parent.width
                implicitHeight: 42
                focusPolicy: Qt.NoFocus
                text: qsTr("Quit game and save")
                background: Rectangle {
                    radius: 8
                    color: Theme.surfaceRaised
                    border.width: 1
                    border.color: Theme.borderButton
                }
                onClicked: root.player.quitGame()
            }
            FbButton {
                objectName: "diagToggle"
                x: Math.max(0, (parent.width - width) / 2)
                width: Math.min(implicitWidth, parent.width)
                kind: "link"
                focusPolicy: Qt.NoFocus
                text: (root.diag.open ? qsTr("▾ Hide diagnostics") : qsTr("▸ Show diagnostics"))
                      + (root.player.controllers.hotkeyLabels.diagnostics !== "" ? "   " + root.player.controllers.hotkeyLabels.diagnostics : "")
                onClicked: root.diag.toggle()
            }
        }
    }
}
