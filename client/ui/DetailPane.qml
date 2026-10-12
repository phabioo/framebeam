import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Detail pane (3c) for the selected game.
Rectangle {
    id: root
    objectName: "detailPane"
    required property PlayerController player
    readonly property var game: player.selectedGame
    readonly property bool hasGame: game.id !== undefined
    // 3c-5: the selected game is the one paused in the background (detail column with Resume / Quit game).
    readonly property bool running: game.running === true
    // "overview" (status, SAVE summary, start checklist) or "saves" (3c-3 saves view in the same 392 column)
    property string mode: "overview"
    readonly property SaveHistoryController hist: player.saveHistory
    // A restore, delete or upload confirmation is open, or an upload runs: Play is disabled (decision af).
    readonly property bool confirming: hist.confirmationOpen
    readonly property string gameId: game.id === undefined ? "" : game.id
    onGameIdChanged: {
        root.leaveSaves()
        if (gameId !== "") fadeIn.restart()
    }
    // Plugins (0.11 design 4h-2): the game code comes from the cached ROM; the plugins matching the selected game feed the
    // status row and are told about the game (empty = none selected) so they can fill their detail section.
    readonly property var pluginsApi: player.plugins
    readonly property string pluginCode: game.id === undefined ? "" : pluginsApi.gameCode(game.system || "", game.cachePath || "")
    readonly property var gamePlugins: {
        const _dep = pluginsApi.plugins
        return hasGame ? pluginsApi.pluginsForGame(game.system || "", pluginCode) : []
    }
    function syncPluginGame() {
        // game.id, not hasGame: this runs from onGameChanged, before the dependent bindings are re-evaluated.
        if (game.id !== undefined) pluginsApi.setDetailGame(game.system || "", pluginCode, game.sha || "", game.title || "")
        else pluginsApi.setDetailGame("", "", "", "")
    }
    onGameChanged: syncPluginGame()
    onPluginCodeChanged: syncPluginGame()
    Component.onCompleted: syncPluginGame()
    readonly property var bg: player.backgroundGame
    readonly property string sinceText: !running || !bg.startedMs ? "" :
        qsTr("%1 · paused %2").arg(Qt.formatTime(new Date(bg.startedMs), "HH:mm")).arg(Qt.formatTime(new Date(bg.pausedMs), "HH:mm"))
    function openSaves(upload) {
        root.mode = "saves"
        savesLoader.active = true
        if (upload === true) Qt.callLater(() => { if (savesLoader.item !== null) savesLoader.item.openUpload() })
        else Qt.callLater(() => { if (savesLoader.item !== null) savesLoader.item.forceActiveFocus() })
    }
    function leaveSaves() {
        if (savesLoader.item !== null) savesLoader.item.cancelAll()
        root.mode = "overview"
    }
    NumberAnimation { id: fadeIn; target: flick; property: "opacity"; from: 0.35; to: 1; duration: Theme.durPage }

    color: Theme.bgPanel
    implicitWidth: Theme.detailWidth

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: Theme.borderSidebar
    }

    FbLabel {
        visible: !root.hasGame
        anchors.centerIn: parent
        text: qsTr("Select a game from the Library.")
        color: Theme.textFaint
        font.pixelSize: Theme.fontSmall
    }

    ColumnLayout {
        visible: root.hasGame
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        Flickable {
            id: flick
            visible: root.mode === "overview"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            contentWidth: width
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                width: flick.width
                spacing: 18

                RowLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: Theme.space16
                    Rectangle {
                        implicitWidth: root.running ? 96 : 112
                        implicitHeight: root.running ? 96 : 112
                        radius: Theme.radius8
                        color: Theme.tile
                        FbLabel {
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: 10
                            text: root.game.monogram || ""
                            font.pixelSize: 34
                            font.weight: Font.DemiBold
                            color: Theme.monogram
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        spacing: Theme.space6
                        FbMono {
                            objectName: "detailCaption"
                            visible: root.game.localFile === true && !root.running
                            Layout.fillWidth: true
                            text: root.game.onHub ? qsTr("LOCAL COPY OF A HUB GAME") : qsTr("LOCAL FILE · NOT ON HUB")
                            color: Theme.accent
                            font.pixelSize: Theme.fontMono
                            elide: Text.ElideRight
                        }
                        FbPill {
                            objectName: "detailPill"
                            visible: (root.game.pillText || "") !== ""
                            Layout.minimumWidth: 0
                            Layout.maximumWidth: parent.width
                            tone: root.game.pillTone || "neutral"
                            text: root.game.pillText || ""
                        }
                        FbLabel {
                            objectName: "detailTitle"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            wrapMode: Text.Wrap
                            text: root.game.title || ""
                            font.pixelSize: Theme.fontDetail
                            font.weight: Font.DemiBold
                        }
                        FbLabel {
                            objectName: "detailSystem"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            text: (root.game.coreVersionText || "") !== ""
                                  ? qsTr("%1 · %2 %3").arg(root.game.systemName || "").arg(root.game.coreLabelText || "").arg(root.game.coreVersionText)
                                  : ((root.game.coreLabelText || "") !== "" ? qsTr("%1 · %2").arg(root.game.systemName || "").arg(root.game.coreLabelText)
                                                                              : (root.game.systemName || ""))
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                        FbLabel {
                            objectName: "detailCoreNotice"
                            visible: (root.game.coreNotice || "") !== ""
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            text: root.game.coreNotice || ""
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontMeta
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Status table
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 0
                    Repeater {
                        model: root.running ? [
                            { label: qsTr("Running since"), text: root.sinceText, tone: "neutral", hint: "" },
                            { label: qsTr("Sharing"), text: root.player.sessions.shared ? qsTr("Shared") : qsTr("Not shared"), tone: "neutral", hint: "" },
                            { label: qsTr("Core"), text: root.game.coreText, tone: root.game.coreTone, hint: "" }
                        ] : root.game.localFile === true ? [
                            // A game that starts from a library folder (3c-7): the file, the core and how it relates to the Hub.
                            { label: qsTr("File"), text: root.game.localPath || "", tone: "neutral", hint: "", objName: "detailLocalPath", mono: true },
                            { label: root.game.onHub ? qsTr("Matches") : qsTr("On the Hub"),
                              text: root.game.onHub ? qsTr("Hub ROM %1").arg(root.game.hubMatchText || "") : qsTr("Not uploaded"), tone: "neutral", hint: "" },
                            { label: qsTr("Core"), text: root.game.coreText, tone: root.game.coreTone, hint: root.game.coreHint }
                        ] : [
                            { label: qsTr("ROM"), text: root.game.romText, tone: root.game.romTone, hint: "" },
                            { label: qsTr("Core"), text: root.game.coreText, tone: root.game.coreTone, hint: root.game.coreHint },
                            { label: qsTr("Firmware"), text: root.game.firmwareText, tone: root.game.firmwareTone, hint: root.game.firmwareHint }
                        ].concat(root.gamePlugins.length > 0 ? [
                            { label: qsTr("Plugins"), text: qsTr("%1 on · %2").arg(root.gamePlugins.length).arg(root.gamePlugins.map(p => p.name).join(", ")), tone: "neutral", hint: "", objName: "detailPluginsRow" }
                        ] : [])
                        delegate: Item {
                            id: row
                            required property var modelData
                            objectName: modelData.objName || ""
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            implicitHeight: rowCol.implicitHeight + 22
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: Theme.borderRow
                            }
                            ColumnLayout {
                                id: rowCol
                                anchors.fill: parent
                                anchors.topMargin: 11
                                anchors.bottomMargin: 11
                                spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 12
                                    FbLabel { text: row.modelData.label; color: Theme.textMuted; font.pixelSize: 13 }
                                    FbLabel {
                                        // The value takes the rest of the row and wraps: a long status text must never widen the pane.
                                        // A file path is cut in the middle instead (the full path is the tooltip).
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        horizontalAlignment: Text.AlignRight
                                        wrapMode: row.modelData.mono === true ? Text.NoWrap : Text.Wrap
                                        elide: row.modelData.mono === true ? Text.ElideMiddle : Text.ElideNone
                                        HoverHandler { id: valHover }
                                        ToolTip.visible: row.modelData.mono === true && valHover.hovered
                                        ToolTip.text: row.modelData.text || ""
                                        ToolTip.delay: 400
                                        objectName: "detailRowValue"
                                        text: (row.modelData.tone === "ok" ? "✓ " : (row.modelData.tone === "warn" ? "▲ " : (row.modelData.tone === "error" ? "✕ " : "")))
                                              + (row.modelData.text || "")
                                        font.pixelSize: Theme.fontSmall
                                        color: row.modelData.tone === "neutral" ? Theme.text : Theme.toneColor(row.modelData.tone)
                                    }
                                }
                                FbMono {
                                    visible: (row.modelData.hint || "") !== ""
                                    Layout.fillWidth: true
                                    text: row.modelData.hint || ""
                                    wrapMode: Text.WrapAnywhere
                                    font.pixelSize: Theme.fontMono
                                }
                            }
                        }
                    }
                }

                SaveSummary {
                    player: root.player
                    Layout.maximumWidth: content.width
                    onManage: root.openSaves(false)
                    onUploadRequested: root.openSaves(true)
                }

                PluginDetailSection {
                    objectName: "pluginDetailSections"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    player: root.player
                }

                FbLabel {
                    objectName: "runningNote"
                    visible: root.running
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    wrapMode: Text.Wrap
                    lineHeight: 1.3
                    color: Theme.textMeta
                    font.pixelSize: Theme.fontMeta
                    text: qsTr("The game stays loaded and paused while you browse, change settings or watch someone else’s session.")
                }
                FbButton {
                    objectName: "firmwareRecheck"
                    visible: root.game.firmwareBlocked === true
                    kind: "link"
                    text: qsTr("Check firmware again")
                    onClicked: root.player.recheckFirmware()
                }
                // Local-only game (0.12): the file stays on a Player until it is uploaded.
                ColumnLayout {
                    objectName: "localOnlyBox"
                    visible: root.game.localOnly === true && !root.running
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 8
                    FbLabel {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        wrapMode: Text.Wrap
                        lineHeight: 1.3
                        color: Theme.textMeta
                        font.pixelSize: Theme.fontMeta
                        text: root.game.localFile === true
                              ? qsTr("Only you see this game. Its file stays on this device until you upload it to the Hub.")
                              : qsTr("Only you see this game. Its file is not on this device and not on the Hub.")
                    }
                    FbLabel {
                        objectName: "localUploadStatus"
                        visible: root.player.upload.gameId === root.game.id && (root.player.upload.active || root.player.upload.message !== "")
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontMeta
                        color: root.player.upload.isError ? Theme.errorText : Theme.text
                        text: root.player.upload.active
                              ? qsTr("Uploading · %1 %").arg(Math.round(root.player.upload.progress * 100))
                              : root.player.upload.message
                    }
                    Rectangle {
                        objectName: "localUploadProgress"
                        visible: root.player.upload.active && root.player.upload.gameId === root.game.id
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4
                        radius: 2
                        color: Theme.surfaceRaised
                        Rectangle {
                            height: parent.height
                            radius: 2
                            width: parent.width * root.player.upload.progress
                            color: Theme.accent
                        }
                    }
                }
                // Start checklist
                ColumnLayout {
                    objectName: "startChecklist"
                    visible: !root.running
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 10
                    Eyebrow { text: qsTr("Start") }
                    Repeater {
                        model: root.game.checklist || []
                        delegate: RowLayout {
                            id: step
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 12
                            Item {
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 18
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: step.modelData.state === "done" ? Theme.accent : (step.modelData.state === "error" ? Theme.pluginWarn : "transparent")
                                    border.width: (step.modelData.state === "done" || step.modelData.state === "error") ? 0 : 1.5
                                    border.color: step.modelData.state === "active" ? Theme.accent : Theme.textDisabled
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 2
                                FbLabel {
                                    Layout.fillWidth: true
                                    text: step.modelData.label
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: Theme.fontSmall
                                    color: step.modelData.state === "pending" ? Theme.textFaint : Theme.text
                                }
                                FbMono {
                                    visible: (step.modelData.meta || "") !== ""
                                    Layout.fillWidth: true
                                    text: step.modelData.meta || ""
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: Theme.fontMono
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    visible: (root.game.error || "") !== "" && !root.running
                    objectName: "startError"
                    Layout.fillWidth: true
                    implicitHeight: errText.implicitHeight + 24
                    radius: Theme.radius8
                    color: Theme.errorBg
                    FbLabel {
                        id: errText
                        anchors.fill: parent
                        anchors.margins: 12
                        text: root.game.error || ""
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontSmall
                        color: Theme.errorText
                    }
                }
            }
        }

        Loader {
            id: savesLoader
            active: false
            visible: root.mode === "saves"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            sourceComponent: SavesView {
                player: root.player
                onBack: root.leaveSaves()
            }
        }

        FbButton {
            objectName: "playButton"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: 48
            kind: "primary"
            font.pixelSize: Theme.fontSection
            busy: root.game.busy === true
            busyOnClick: true
            text: root.running ? qsTr("▶ Resume") : (root.game.playLabel || qsTr("Play"))
            enabled: root.game.canPlay === true && !root.confirming && !(root.running && root.player.quitting)
            onClicked: root.game.running === true ? root.player.resumeGame() : root.player.playSelected()
        }
        // Why Play is off, directly under the button (3c-7).
        FbLabel {
            objectName: "playDisabledReason"
            visible: !root.running && (root.game.playDisabledReason || "") !== "" && root.game.canPlay !== true
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            Layout.topMargin: -10
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.pluginWarn
            font.pixelSize: Theme.fontMeta
            text: root.game.playDisabledReason || ""
        }
        // Secondary actions: two equal buttons for a local file (3c-7: Upload to Hub / Play and share, Remove from library) and for a
        // Hub that cannot be reached (Retry connection, Hub settings); otherwise the one wide "Play and share Session" below.
        RowLayout {
            id: secondaryRow
            objectName: "detailSecondaryRow"
            readonly property bool offlineError: !root.running && root.game.hubOffline === true && (root.game.playDisabledReason || "") !== ""
            readonly property bool localRow: !root.running && root.game.localOnly === true || (!root.running && root.game.localFile === true)
            visible: !root.running && (offlineError || localRow)
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 8
            FbButton {
                objectName: secondaryRow.offlineError ? "retryConnectionButton"
                            : (root.game.onHub ? "playShareButton" : "uploadToHubButton")
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.minimumWidth: 0
                implicitHeight: 40
                implicitWidth: 0
                hPadding: 16
                font.pixelSize: Theme.fontSmall
                text: secondaryRow.offlineError ? qsTr("Retry connection")
                      : (root.game.onHub ? qsTr("Play and share") : qsTr("Upload to Hub"))
                enabled: secondaryRow.offlineError ? true
                         : (root.game.onHub ? (root.game.canPlay === true && !root.confirming && root.player.sessions.available)
                                            : (root.player.canUpload && root.game.localFile === true && !root.player.upload.active))
                onClicked: secondaryRow.offlineError ? root.player.retryConnection()
                           : (root.game.onHub ? root.player.playAndShareSelected() : root.player.uploadGameToHub(root.game.id))
            }
            FbButton {
                objectName: secondaryRow.offlineError ? "hubSettingsButton" : "removeFromLibraryButton"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.minimumWidth: 0
                implicitHeight: 40
                implicitWidth: 0
                hPadding: 16
                font.pixelSize: Theme.fontSmall
                text: secondaryRow.offlineError ? qsTr("Hub settings") : qsTr("Remove from library")
                enabled: secondaryRow.offlineError ? true : !root.player.upload.active
                onClicked: secondaryRow.offlineError ? root.player.showSettings() : root.player.removeFromLibrary(root.game.id)
            }
        }
        FbButton {
            objectName: "playShareButton"
            visible: !secondaryRow.visible && (root.player.sessions.available || root.running)
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: 44
            text: root.game.running === true ? qsTr("Quit game") : qsTr("Play and share Session")
            enabled: (root.running && !root.player.quitting) || (root.game.canPlay === true && !root.confirming)
            onClicked: root.running ? root.player.requestQuit() : root.player.playAndShareSelected()
        }
        FbLabel {
            objectName: "quitNote"
            visible: root.running
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.textFaint
            font.pixelSize: Theme.fontMeta
            text: root.player.quitting ? qsTr("Saving and syncing…") : qsTr("Quit saves and syncs to the hub first")
        }
        FbLabel {
            objectName: "shareVisibilityHint"
            visible: root.player.sessions.available && root.game.running !== true
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textFaint
            font.pixelSize: Theme.fontMeta
            text: qsTr("Visibility: %1 · change it in the Session panel")
                  .arg(root.player.sessions.visibility === "private" ? qsTr("Private")
                       : root.player.sessions.visibility === "invite_only" ? qsTr("Invite only") : qsTr("Hub users"))
        }
    }
}
