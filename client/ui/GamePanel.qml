import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// GamePanel (docs/design/player.md "GamePanel", 3g-2/3g-3, 3r-2, 3h-2, 3i-2). Side panel of the game view, 340 wide (320 at
// 1280), collapsible to a 48 px rail. Your game: SHARING, SAVE and "Quit game" pinned at the bottom. In the Multiview the
// panel follows the selected tile: your tile = the full panel with a tile header, a remote tile = who, connection, "Audio
// here", "Remove from multiview".
Rectangle {
    id: root
    required property PlayerController player
    readonly property SessionController ctl: player.sessions
    readonly property SaveHistoryController saves: player.saveHistory
    readonly property GameSession gameSession: player.gameSession
    property bool compact: false
    property bool collapsed: false
    property bool multi: false
    property string tile: "local"
    readonly property bool localTile: tile === "local"
    readonly property int tileNo: ctl.surfaceOrder.indexOf(tile) + 1
    readonly property var tileInfo: ctl.surfaceInfo[tile] || ({})
    readonly property var tileLink: ctl.surfaceLinks[tile] || null
    readonly property string hubWord: qsTr("this Hub")
    readonly property bool quitting: player.quitting
    readonly property string shareText: ctl.shared ? qsTr("Shared · %1 watching").arg(ctl.viewerCount) : qsTr("Not shared")
    signal collapseToggled()
    signal manageSavesRequested()
    signal selectLocalRequested()

    objectName: "gamePanel"
    color: Theme.gameHeader
    implicitWidth: collapsed ? 48 : (compact ? 320 : 340)
    clip: true

    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.gameBorder }

    function slotLabel() {
        var opts = saves.slotOptions
        for (var i = 0; i < opts.length; ++i) {
            if (opts[i].value === saves.slot) return opts[i].label
        }
        return saves.slot === "default" ? qsTr("default") : saves.slot
    }
    function sinceText(iso) {
        var t = Date.parse(iso)
        if (isNaN(t)) return ""
        var mins = Math.max(0, Math.floor((Date.now() - t) / 60000))
        return mins < 1 ? qsTr("just started") : qsTr("for %1 min").arg(mins)
    }

    // ---- collapsed rail ----
    Column {
        objectName: "panelRail"
        visible: root.collapsed
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 14
        GameButton {
            objectName: "panelExpandButton"
            anchors.horizontalCenter: parent.horizontalCenter
            look: "surface"
            fixedWidth: 32
            implicitHeight: 32
            glyph: "⇤"
            tip: qsTr("Show panel")
            onClicked: root.collapseToggled()
        }
        Rectangle {
            visible: root.multi
            anchors.horizontalCenter: parent.horizontalCenter
            width: 20; height: 20; radius: 4
            color: Theme.gameSelection
            FbMono { anchors.centerIn: parent; text: String(root.tileNo); font.pixelSize: 11; color: "#121315" }
        }
        Rectangle {
            id: railDot
            objectName: "panelRailShareDot"
            anchors.horizontalCenter: parent.horizontalCenter
            width: 8; height: 8; radius: 4
            color: root.ctl.shared ? Theme.gameOk : Theme.gameMeta
            HoverHandler { id: dotHover }
            ToolTip.visible: dotHover.hovered
            ToolTip.text: root.shareText
        }
        GameIcon {
            visible: root.localTile
            anchors.horizontalCenter: parent.horizontalCenter
            kind: "diamond"
            color: Theme.gameMeta
            HoverHandler { id: diaHover }
            ToolTip.visible: diaHover.hovered
            ToolTip.text: qsTr("Save snapshot · %1").arg(root.player.controllers.hotkeyLabels.snapshot)
        }
    }

    ColumnLayout {
        visible: !root.collapsed
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0

        // Tile header (Multiview)
        Rectangle {
            objectName: "panelTileHeader"
            Layout.fillWidth: true
            visible: root.multi
            implicitHeight: 52
            color: "transparent"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.gameBorder }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 16
                spacing: 10
                Rectangle {
                    objectName: "panelTileBadge"
                    Layout.preferredWidth: 20; Layout.preferredHeight: 20; radius: 4
                    color: Theme.gameSelection
                    FbMono { anchors.centerIn: parent; text: String(root.tileNo); font.pixelSize: 11; color: "#121315" }
                }
                FbMono {
                    objectName: "panelTileLabel"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                    text: root.localTile ? qsTr("TILE %1 · YOUR GAME").arg(root.tileNo) : qsTr("TILE %1 · WATCHING").arg(root.tileNo)
                    font.pixelSize: 11
                    font.letterSpacing: 0.9
                    color: Theme.gameBadgeText
                }
                GameButton {
                    objectName: "panelCollapseButton"
                    look: "surface"
                    fixedWidth: 28
                    implicitHeight: 28
                    glyph: "⇥"
                    tip: qsTr("Hide panel")
                    onClicked: root.collapseToggled()
                }
            }
        }

        // ---- your game ----
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.localTile
            contentWidth: width
            contentHeight: col.implicitHeight + 40
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            // Plain Columns whose children take exactly the block width: a layout would size its cell to the widest
            // minimum of any child, so one item that cannot shrink (wide platform fonts) would push the panel's buttons
            // out (seen with Qt 6.8 on Windows).
            Column {
                id: col
                x: 24
                y: 20
                width: flick.width - 48
                spacing: 20

                Rectangle {
                    objectName: "panelHubBanner"
                    width: col.width
                    visible: root.ctl.hubLink !== "online"
                    implicitHeight: bannerText.implicitHeight + 16
                    height: implicitHeight
                    radius: 8
                    color: Theme.gameOnBg
                    FbLabel {
                        id: bannerText
                        anchors.fill: parent
                        anchors.margins: 8
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: Theme.gameAccent
                        text: root.ctl.shared ? qsTr("Hub not reachable · reconnecting. The running Session continues until the Hub ends it.")
                                              : qsTr("Hub not reachable · reconnecting. Sharing needs a Hub connection.")
                    }
                }
                Rectangle {
                    objectName: "panelMessage"
                    width: col.width
                    visible: root.ctl.message !== ""
                    implicitHeight: msgText.implicitHeight + 16
                    height: implicitHeight
                    radius: 8
                    color: root.ctl.messageIsError ? Theme.gameDangerBg : Theme.gameHover
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

                // Audio of this tile (Multiview; your tile has no remote-style panel, so the control lives here)
                GameButton {
                    objectName: "audioButton_local"
                    visible: root.multi && root.ctl.hasLocalGame
                    width: col.width
                    implicitHeight: 36
                    look: "outlined"
                    on: root.ctl.audioFocus === "local"
                    enabled: !on
                    text: on ? qsTr("♪ Audio plays from this tile") : qsTr("♪ Audio here")
                    onClicked: root.ctl.audioHere("local")
                }

                // SHARING
                Column {
                    id: shareBlock
                    objectName: "sharingBlock"
                    width: col.width
                    spacing: 10
                    RowLayout {
                        width: parent.width
                        spacing: 8
                        Eyebrow { Layout.fillWidth: true; Layout.minimumWidth: 0; text: qsTr("Sharing") }
                        SharePill {
                            objectName: "panelShareState"
                            bare: true
                            on: root.ctl.shared
                            text: root.shareText
                        }
                        GameButton {
                            objectName: "panelCollapseButtonSession"
                            visible: !root.multi
                            look: "surface"
                            fixedWidth: 28
                            implicitHeight: 28
                            glyph: "⇥"
                            tip: qsTr("Hide panel")
                            onClicked: root.collapseToggled()
                        }
                    }
                    GameSegment {
                        objectName: "visibilitySegment"
                        width: parent.width
                        bordered: true
                        stretch: true
                        padX: 6
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
                        lineHeight: 1.25
                        font.pixelSize: 12
                        color: Theme.gameMeta
                        text: {
                            var v = root.ctl.visibility
                            if (root.ctl.shared)
                                return v === "invite_only" ? qsTr("Only invited users can watch. They send no input and cannot invite others.")
                                                           : qsTr("Everyone on %1 can watch. Viewers send no input.").arg(root.hubWord)
                            if (v === "private") return qsTr("Only you can see this game. Choose Hub users or Invite only, then share.")
                            return v === "hub_users" ? qsTr("Not shared yet. Share to let hub users watch.") : qsTr("Not shared yet. Share to let invited users watch.")
                        }
                    }
                    FbButton {
                        objectName: "shareButton"
                        visible: !root.ctl.shared
                        width: parent.width
                        implicitHeight: 40
                        focusPolicy: Qt.NoFocus
                        kind: "primary"
                        text: root.ctl.shareBusy ? qsTr("Sharing…") : qsTr("Share Session")
                        enabled: !root.ctl.shareBusy && root.ctl.available && root.ctl.hubLink === "online"
                        onClicked: root.ctl.shareSession()
                    }

                    // Watchers and invited users (only while shared)
                    Column {
                        id: partBlock
                        objectName: "participantsBlock"
                        width: parent.width
                        visible: root.ctl.shared
                        spacing: 6
                        FbLabel {
                            objectName: "participantsTitle"
                            width: parent.width
                            elide: Text.ElideRight
                            text: root.ctl.participantsTitle
                            font.pixelSize: 11
                            font.family: Theme.mono
                            font.letterSpacing: 0.9
                            color: Theme.gameFaint
                            visible: root.ctl.participants.length > 0
                        }
                        FbLabel {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            visible: root.ctl.participants.length === 0 && root.ctl.visibility === "invite_only"
                            text: qsTr("Nobody invited yet.")
                            font.pixelSize: 12
                            color: Theme.gameMeta
                        }
                        Rectangle {
                            width: parent.width
                            visible: root.ctl.participants.length > 0
                            implicitHeight: plist.implicitHeight + 2
                            height: implicitHeight
                            radius: 8
                            color: Theme.gameGroupBg
                            border.width: 1
                            border.color: Theme.gameGroupBorder
                            Column {
                                id: plist
                                x: 1
                                y: 1
                                width: parent.width - 2
                                Repeater {
                                    model: root.ctl.participants
                                    // Anchored row: the action is pinned to the right edge, the text takes the rest and
                                    // elides, so the action cannot leave the panel.
                                    delegate: Item {
                                        id: prow
                                        required property var modelData
                                        required property int index
                                        objectName: "participant_" + modelData.id
                                        width: plist.width
                                        implicitHeight: 44
                                        height: 44
                                        Rectangle {
                                            visible: prow.index < root.ctl.participants.length - 1
                                            anchors.bottom: parent.bottom
                                            width: parent.width
                                            height: 1
                                            color: Theme.gameGroupBorder
                                        }
                                        Rectangle {
                                            id: pav
                                            anchors.left: parent.left
                                            anchors.leftMargin: 10
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 26; height: 26; radius: 13
                                            color: Theme.gameSegmentOn
                                            FbLabel { anchors.centerIn: parent; text: ((prow.modelData.name || "?").charAt(0)).toUpperCase(); font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.gameText }
                                        }
                                        FbButton {
                                            id: pbtn
                                            objectName: (prow.modelData.kind === "viewer" ? "remove_" : "withdraw_") + prow.modelData.id
                                            kind: "link"
                                            anchors.right: parent.right
                                            anchors.rightMargin: 6
                                            anchors.verticalCenter: parent.verticalCenter
                                            implicitHeight: 28
                                            implicitWidth: Math.max(48, implicitContentWidth + 16)
                                            font.pixelSize: 12
                                            focusPolicy: Qt.NoFocus
                                            text: prow.modelData.action
                                            contentItem: Text {
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                                text: pbtn.text
                                                font: pbtn.font
                                                color: pbtn.hovered ? Theme.gameText : Theme.gameTextMuted
                                                textFormat: Text.PlainText
                                                Rectangle { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; width: parent.contentWidth; height: 1; color: parent.color }
                                            }
                                            onClicked: prow.modelData.kind === "viewer" ? root.ctl.removeViewer(prow.modelData.id)
                                                                                       : root.ctl.withdrawInvite(prow.modelData.id)
                                        }
                                        FbPill {
                                            id: ppill
                                            objectName: "participantPill"
                                            anchors.right: pbtn.left
                                            anchors.rightMargin: 4
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: prow.modelData.kind === "viewer" && !!prow.modelData.connection
                                            small: true
                                            text: prow.modelData.connection || ""
                                            tone: prow.modelData.connectionTone || "neutral"
                                        }
                                        ColumnLayout {
                                            anchors.left: pav.right
                                            anchors.leftMargin: 10
                                            anchors.right: ppill.visible ? ppill.left : pbtn.left
                                            anchors.rightMargin: 6
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 0
                                            FbLabel { Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight; text: prow.modelData.name; font.pixelSize: 13; color: Theme.gameText }
                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 5
                                                Rectangle { implicitWidth: 5; implicitHeight: 5; radius: 2.5; color: prow.modelData.online ? Theme.gameOk : Theme.gameMeta }
                                                FbLabel { objectName: "participantStatus"; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight; text: prow.modelData.status; font.pixelSize: 11; color: Theme.gameMeta }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        // Relay hint (D9)
                        Rectangle {
                            objectName: "relayHint"
                            width: parent.width
                            visible: root.ctl.relayHint !== ""
                            implicitHeight: relayRow.implicitHeight + 18
                            height: implicitHeight
                            radius: 8
                            color: Theme.infoBg
                            border.width: 1
                            border.color: Theme.infoBorder
                            RowLayout {
                                id: relayRow
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 8
                                FbLabel { text: "ⓘ"; font.pixelSize: 12; color: Theme.gameAccent; Layout.alignment: Qt.AlignTop }
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

                    // Invite a hub user (Invite only, shared)
                    Column {
                        id: invBlock
                        objectName: "inviteBlock"
                        width: parent.width
                        visible: root.ctl.shared && root.ctl.visibility === "invite_only"
                        spacing: 8
                        FbField {
                            id: inviteField
                            objectName: "inviteField"
                            width: parent.width
                            implicitHeight: 34
                            font.family: Qt.application.font.family
                            color: Theme.gameText
                            placeholderText: qsTr("+ Invite a hub user…")
                            background: Rectangle {
                                radius: 7
                                color: "transparent"
                                border.width: 1
                                border.color: inviteField.activeFocus ? Theme.gameAccent : Theme.gameKeyLine
                            }
                            onTextChanged: root.ctl.searchUsers(text)
                        }
                        FbLabel {
                            objectName: "userSearchHint"
                            width: parent.width
                            visible: root.ctl.userSearchHint.length > 0
                            wrapMode: Text.WordWrap
                            text: root.ctl.userSearchHint
                            font.pixelSize: 12
                            color: Theme.gameMeta
                        }
                        Repeater {
                            model: root.ctl.userResults
                            delegate: RowLayout {
                                id: urow
                                required property var modelData
                                objectName: "userResult_" + modelData.id
                                width: invBlock.width
                                spacing: 10
                                Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: urow.modelData.online ? Theme.gameOk : Theme.gameMeta }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Layout.preferredWidth: 0
                                    spacing: 0
                                    FbLabel { Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight; text: urow.modelData.name; font.pixelSize: 13; color: Theme.gameText }
                                    FbLabel {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: urow.modelData.hint
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: 11
                                        color: Theme.gameMeta
                                    }
                                }
                                FbButton {
                                    objectName: "invite_" + urow.modelData.id
                                    implicitHeight: 28
                                    kind: "primary"
                                    font.pixelSize: 12
                                    focusPolicy: Qt.NoFocus
                                    text: qsTr("Invite")
                                    onClicked: { root.ctl.invite(urow.modelData.id); inviteField.text = "" }
                                }
                            }
                        }
                    }

                    FbButton {
                        objectName: "shareButton"
                        visible: root.ctl.shared
                        width: parent.width
                        implicitHeight: 36
                        focusPolicy: Qt.NoFocus
                        text: qsTr("Stop sharing")
                        onClicked: root.ctl.stopSharing()
                    }
                }

                Rectangle { width: col.width; height: 1; color: Theme.gameBorder; visible: saveBlock.visible }

                // SAVE
                Column {
                    id: saveBlock
                    objectName: "saveSlotBlock"
                    width: col.width
                    visible: root.gameSession.active && root.saves.slotsAvailable
                    spacing: 10
                    RowLayout {
                        width: parent.width
                        Eyebrow { Layout.fillWidth: true; Layout.minimumWidth: 0; text: qsTr("Save") }
                        FbButton {
                            id: manageBtn
                            objectName: "manageSavesButton"
                            kind: "link"
                            focusPolicy: Qt.NoFocus
                            implicitHeight: 24
                            font.pixelSize: 12
                            text: qsTr("Manage saves →")
                            contentItem: Text {
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                                text: manageBtn.text
                                font: manageBtn.font
                                color: manageBtn.hovered ? Theme.gameText : Theme.gameBadgeText
                            }
                            onClicked: root.manageSavesRequested()
                        }
                    }
                    RowLayout {
                        width: parent.width
                        spacing: 8
                        FbLabel {
                            objectName: "saveSlotLabel"
                            Layout.minimumWidth: 0
                            elide: Text.ElideRight
                            text: root.slotLabel()
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: Theme.gameText
                        }
                        FbMono {
                            objectName: "saveVersionLabel"
                            visible: root.saves.history.length > 0
                            text: root.saves.history.length > 0 ? root.saves.history[0].versionText : ""
                            font.pixelSize: 12
                            color: Theme.gameTextMuted
                        }
                        Item { Layout.fillWidth: true }
                        FbLabel {
                            Layout.minimumWidth: 0
                            elide: Text.ElideRight
                            text: qsTr("✓ synced to hub")
                            font.pixelSize: 12
                            color: Theme.gameOk
                        }
                    }
                    GameButton {
                        objectName: "gameSnapshotButton"
                        width: parent.width
                        implicitHeight: 36
                        look: "outlined"
                        iconKind: "diamond"
                        text: root.saves.busy ? qsTr("Saving snapshot…") : qsTr("Save snapshot")
                        hint: root.player.controllers.hotkeyLabels.snapshot
                        enabled: root.saves.available && !root.saves.busy
                        onClicked: root.saves.createSnapshotInGame("")
                    }
                    FbLabel {
                        objectName: "gameSnapshotMessage"
                        width: parent.width
                        wrapMode: Text.WordWrap
                        lineHeight: 1.25
                        font.pixelSize: 12
                        text: root.saves.message !== "" ? root.saves.message
                              : qsTr("Saved to “%1” · final sync on pause or quit").arg(root.slotLabel())
                        color: root.saves.message === "" ? Theme.gameMeta : (root.saves.messageIsError ? Theme.gameDanger : Theme.gameOk)
                    }
                }
            }
        }

        // Quit game: pinned at the bottom of your game's panel
        Rectangle {
            Layout.fillWidth: true
            visible: root.localTile
            implicitHeight: quitCol.implicitHeight + 36
            color: "transparent"
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.gameBorder }
            Column {
                id: quitCol
                x: 24
                y: 16
                width: parent.width - 48
                spacing: 8
                FbButton {
                    objectName: "endGameButton"
                    visible: !root.quitting
                    width: parent.width
                    implicitHeight: 40
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Quit game")
                    background: Rectangle { radius: 8; color: parent.hovered ? Theme.gameSegmentOn : Theme.gameHover }
                    onClicked: root.player.requestQuit()
                }
                FbLabel {
                    objectName: "quitNote"
                    visible: !root.quitting
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Saves and syncs first")
                    font.pixelSize: 12
                    color: Theme.gameFaint
                }
                Rectangle {
                    objectName: "quittingBox"
                    visible: root.quitting
                    width: parent.width
                    implicitHeight: qcol.implicitHeight + 24
                    height: implicitHeight
                    radius: 8
                    color: Theme.gameTrack
                    border.width: 1
                    border.color: Theme.gameGroupBorder
                    ColumnLayout {
                        id: qcol
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6
                        RowLayout {
                            spacing: 8
                            FbSpinner { size: 14; color: Theme.gameAccent }
                            FbLabel { Layout.fillWidth: true; Layout.minimumWidth: 0; text: qsTr("Saving and syncing…"); font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.gameText; elide: Text.ElideRight }
                        }
                        FbLabel { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Returns to the Library when done."); font.pixelSize: 12; color: Theme.gameFaint }
                    }
                }
            }
        }

        // ---- remote tile ----
        Item {
            id: remoteBox
            objectName: "remotePanel"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.localTile
            Column {
                x: 24
                y: 20
                width: parent.width - 48
                spacing: 16
                RowLayout {
                    width: parent.width
                    spacing: 12
                    Rectangle {
                        Layout.preferredWidth: 36; Layout.preferredHeight: 36; radius: 18
                        color: Theme.gameSegmentOn
                        FbLabel { anchors.centerIn: parent; text: ((root.tileInfo.who || "?").charAt(0)).toUpperCase(); font.pixelSize: 14; font.weight: Font.DemiBold; color: Theme.gameText }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 0
                        spacing: 0
                        FbLabel { objectName: "remoteWho"; Layout.fillWidth: true; elide: Text.ElideRight; text: root.tileInfo.who || ""; font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.gameText }
                        FbLabel { objectName: "remoteGame"; Layout.fillWidth: true; elide: Text.ElideRight; text: root.tileInfo.game || ""; font.pixelSize: 13; color: Theme.gameTextMuted }
                    }
                }
                RowLayout {
                    width: parent.width
                    spacing: 10
                    FbPill {
                        objectName: "remoteLinkPill"
                        visible: root.tileLink !== null
                        small: true
                        text: root.tileLink ? (root.tileLink.text + (root.tileLink.rtt ? " · " + root.tileLink.rtt : "")) : ""
                        tone: root.tileLink ? root.tileLink.tone : "neutral"
                    }
                    FbLabel {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        elide: Text.ElideRight
                        text: (root.tileInfo.visibility || "") + (root.sinceText(root.tileInfo.since || "") !== "" ? " · " + root.sinceText(root.tileInfo.since || "") : "")
                        font.pixelSize: 12
                        color: Theme.gameMeta
                    }
                }
                GameButton {
                    objectName: "audioButton_" + root.tile
                    width: parent.width
                    implicitHeight: 36
                    look: "outlined"
                    readonly property bool here: root.ctl.audioFocus === root.tile
                    on: here
                    enabled: !here
                    text: here ? qsTr("♪ Audio plays from this tile") : qsTr("♪ Audio here")
                    onClicked: root.ctl.audioHere(root.tile)
                }
                GameButton {
                    objectName: "removeButton_" + root.tile
                    width: parent.width
                    implicitHeight: 36
                    look: "outlined"
                    text: qsTr("Remove from multiview")
                    onClicked: root.ctl.removeSurface(root.tile)
                }
                FbLabel {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    lineHeight: 1.25
                    font.pixelSize: 12
                    color: Theme.gameMeta
                    text: qsTr("You only watch and listen. %1 sees you as a viewer.").arg(root.tileInfo.who || qsTr("The host"))
                }
            }
            Rectangle {
                visible: root.ctl.hasLocalGame
                anchors.bottom: parent.bottom
                width: parent.width
                implicitHeight: 52
                height: 52
                color: "transparent"
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.gameBorder }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 24
                    anchors.rightMargin: 24
                    spacing: 10
                    Rectangle {
                        Layout.preferredWidth: 20; Layout.preferredHeight: 20; radius: 4
                        color: Theme.gameBadge
                        FbMono { anchors.centerIn: parent; text: String(root.ctl.surfaceOrder.indexOf("local") + 1); font.pixelSize: 11; color: Theme.gameBadgeText }
                    }
                    FbLabel {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        elide: Text.ElideRight
                        text: qsTr("Your game runs in tile %1").arg(root.ctl.surfaceOrder.indexOf("local") + 1)
                        font.pixelSize: 12
                        color: Theme.gameTextMuted
                    }
                    FbButton {
                        id: selBtn
                        objectName: "selectLocalTile"
                        kind: "link"
                        focusPolicy: Qt.NoFocus
                        implicitHeight: 28
                        font.pixelSize: 12
                        text: qsTr("Select")
                        contentItem: Text {
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            text: selBtn.text
                            font: selBtn.font
                            color: Theme.gameText
                            Rectangle { anchors.bottom: parent.bottom; anchors.right: parent.right; width: parent.contentWidth; height: 1; color: parent.color }
                        }
                        onClicked: root.selectLocalRequested()
                    }
                }
            }
        }
    }
}
