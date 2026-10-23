import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

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

    color: Theme.ground
    implicitWidth: Theme.detailWidth

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: Theme.rule
    }

    UzLabel {
        objectName: "detailEmptyHint"
        // An empty Library already says what to do in its own column: no second hint with nothing to select (R-P14).
        visible: !root.hasGame && root.player.library.totalCount > 0
        anchors.centerIn: parent
        text: qsTr("Select a game from the Library.")
        color: Theme.dim
        font.pixelSize: Theme.roles.input.size
    }

    ColumnLayout {
        visible: root.hasGame
        anchors.fill: parent
        anchors.margins: Theme.space3
        spacing: Theme.space2

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
                spacing: Theme.space2

                RowLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: Theme.space2
                    // Cover placeholder (design-system.md "GameCover"): `placeholder` plane, 1 px `placeholder-rule`, monogram.
                    Rectangle {
                        implicitWidth: root.running ? 96 : 112
                        implicitHeight: root.running ? 96 : 112
                        color: Theme.placeholder
                        border.width: 1
                        border.color: Theme.placeholderRule
                        UzLabel {
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: Theme.space1
                            text: root.game.monogram || ""
                            font.pixelSize: 34
                            font.weight: Font.DemiBold
                            color: Theme.placeholderRule
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        spacing: Theme.space1
                        UzMono {
                            objectName: "detailCaption"
                            visible: root.game.localFile === true && !root.running
                            Layout.fillWidth: true
                            text: root.game.onHub ? qsTr("LOCAL COPY OF A HUB GAME") : qsTr("LOCAL FILE · NOT ON HUB")
                            color: Theme.accentText
                            font.pixelSize: Theme.roles.meta11.size
                            Layout.preferredWidth: 1
                            wrapMode: Text.WordWrap   // wraps instead of "LOCAL COPY OF A HUB GA…" at 960 (R-P22)
                        }
                        UzTag {
                            objectName: "detailPill"
                            visible: (root.game.pillText || "") !== ""
                            Layout.minimumWidth: 0
                            Layout.maximumWidth: parent.width
                            tone: root.game.pillTone || "neutral"
                            text: root.game.pillText || ""
                        }
                        UzLabel {
                            objectName: "detailTitle"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            wrapMode: Text.Wrap
                            text: root.game.title || ""
                            font.pixelSize: Theme.roles.dialogTitle.size
                            font.weight: Theme.roles.dialogTitle.weight
                            font.letterSpacing: Theme.roles.dialogTitle.spacing
                        }
                        // Meta line of the game page: `SYSTEM · CORE VERSION` in the Mono meta role.
                        UzMono {
                            objectName: "detailSystem"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            text: (root.game.coreVersionText || "") !== ""
                                  ? qsTr("%1 · %2 %3").arg(root.game.systemName || "").arg(root.game.coreLabelText || "").arg(root.game.coreVersionText)
                                  : ((root.game.coreLabelText || "") !== "" ? qsTr("%1 · %2").arg(root.game.systemName || "").arg(root.game.coreLabelText)
                                                                              : (root.game.systemName || ""))
                            color: Theme.muted
                            font.pixelSize: Theme.roles.meta.size
                            font.letterSpacing: Theme.roles.meta.spacing
                            font.capitalization: Font.AllUppercase
                            elide: Text.ElideRight
                            HoverHandler { id: systemHover }
                            ToolTip.visible: systemHover.hovered && truncated
                            ToolTip.text: text
                            ToolTip.delay: 400
                        }
                        UzLabel {
                            objectName: "detailCoreNotice"
                            visible: (root.game.coreNotice || "") !== ""
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            text: root.game.coreNotice || ""
                            color: Theme.muted
                            font.pixelSize: Theme.roles.small.size
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Status table
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 0
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.rule }
                    Repeater {
                        model: root.running ? [
                            { label: qsTr("Running since"), text: root.sinceText, tone: "neutral", hint: "" },
                            { label: qsTr("Sharing"), text: root.player.sessions.shared ? qsTr("Shared") : qsTr("Not shared"), tone: "neutral", hint: "" },
                            { label: qsTr("Core"), text: root.game.coreText, tone: root.game.coreTone, hint: "" }
                        ] : root.game.localFile === true ? [
                            // A game that starts from a library folder (3c-7): the file, the core and how it relates to the Hub.
                            { label: qsTr("File"), text: root.game.localPath || "", tone: "neutral", hint: "", objName: "detailLocalPath", mono: true },
                            { label: root.game.onHub ? qsTr("Matches") : qsTr("On the server"),
                              text: root.game.onHub ? qsTr("Server ROM %1").arg(root.game.hubMatchText || "") : qsTr("Not uploaded"), tone: "neutral", hint: "" },
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
                            implicitHeight: rowCol.height + 2 * Theme.space1
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: Theme.rule2
                            }
                            // Plain anchors and a Column, not nested layouts: the wrapped value used to get a stale height (hundreds of
                            // pixels, from a 1 px wide first layout pass) that the layouts never recomputed (R-P1).
                            Column {
                                id: rowCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.topMargin: Theme.space1
                                spacing: Theme.space05
                                Item {
                                    width: parent.width
                                    height: Math.max(rowLabel.implicitHeight, rowValue.implicitHeight)
                                    // Label column of the facts table (board S3c1): Mono section-label role, 96 px, value to its right.
                                    UzMono {
                                        id: rowLabel
                                        anchors.left: parent.left
                                        width: 96
                                        text: row.modelData.label
                                        elide: Text.ElideRight
                                        color: Theme.dim
                                        font.pixelSize: Theme.roles.sectionLabel.size
                                        font.weight: Theme.roles.sectionLabel.weight
                                        font.letterSpacing: Theme.roles.sectionLabel.spacing
                                        font.capitalization: Font.AllUppercase
                                    }
                                    UzLabel {
                                        id: rowValue
                                        // The value takes the rest of the row and wraps: a long status text must never widen the pane.
                                        // A file path is cut in the middle instead (the full path is the tooltip).
                                        anchors.left: rowLabel.right
                                        anchors.leftMargin: Theme.space15
                                        anchors.right: parent.right
                                        horizontalAlignment: Text.AlignLeft
                                        wrapMode: row.modelData.mono === true ? Text.NoWrap : Text.Wrap
                                        elide: row.modelData.mono === true ? Text.ElideMiddle : Text.ElideNone
                                        HoverHandler { id: valHover }
                                        ToolTip.visible: row.modelData.mono === true && valHover.hovered
                                        ToolTip.text: row.modelData.text || ""
                                        ToolTip.delay: 400
                                        objectName: "detailRowValue"
                                        text: (row.modelData.tone === "ok" ? "✓ " : ((row.modelData.tone === "warn" || row.modelData.tone === "amber") ? "▲ " : (row.modelData.tone === "error" ? "✕ " : "")))
                                              + (row.modelData.text || "")
                                        font.pixelSize: Theme.roles.input.size
                                        color: row.modelData.tone === "neutral" ? Theme.ink2 : Theme.toneColor(row.modelData.tone)
                                    }
                                }
                                UzMono {
                                    visible: (row.modelData.hint || "") !== ""
                                    x: 96 + Theme.space15
                                    width: parent.width - x
                                    text: row.modelData.hint || ""
                                    wrapMode: Text.WrapAnywhere
                                    font.pixelSize: Theme.roles.meta11.size
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

                UzLabel {
                    objectName: "runningNote"
                    visible: root.running
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    wrapMode: Text.Wrap
                    lineHeight: 18
                    lineHeightMode: Text.FixedHeight
                    color: Theme.muted
                    font.pixelSize: Theme.roles.small.size
                    text: qsTr("The game stays loaded and paused while you browse, change settings or watch someone else’s session.")
                }
                UzButton {
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
                    spacing: Theme.space1
                    UzLabel {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        wrapMode: Text.Wrap
                        lineHeight: 18
                    lineHeightMode: Text.FixedHeight
                        color: Theme.muted
                        font.pixelSize: Theme.roles.small.size
                        text: root.game.localFile === true
                              ? qsTr("Only you see this game. Its file stays on this device until you upload it to the server.")
                              : qsTr("Only you see this game. Its file is not on this device and not on the server.")
                    }
                    UzLabel {
                        objectName: "localUploadStatus"
                        visible: root.player.upload.gameId === root.game.id && (root.player.upload.active || root.player.upload.message !== "")
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.roles.small.size
                        color: root.player.upload.isError ? Theme.danger : Theme.ink
                        text: root.player.upload.active
                              ? qsTr("Uploading · %1 %").arg(Math.round(root.player.upload.progress * 100))
                              : root.player.upload.message
                    }
                    Rectangle {
                        objectName: "localUploadProgress"
                        visible: root.player.upload.active && root.player.upload.gameId === root.game.id
                        Layout.fillWidth: true
                        Layout.preferredHeight: 2
                        color: Theme.rule
                        Rectangle {
                            height: parent.height
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
                    spacing: Theme.space15
                    Eyebrow { text: qsTr("Start") }
                    Repeater {
                        model: root.game.checklist || []
                        delegate: RowLayout {
                            id: step
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: Theme.space15
                            Item {
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 18
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 10
                                    height: 10
                                    radius: 0
                                    color: step.modelData.state === "done" ? Theme.accent : (step.modelData.state === "error" ? Theme.accentText : "transparent")
                                    border.width: (step.modelData.state === "done" || step.modelData.state === "error") ? 0 : 1.5
                                    border.color: step.modelData.state === "active" ? Theme.accent : Theme.disabledInk
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 2
                                UzLabel {
                                    Layout.fillWidth: true
                                    text: step.modelData.label
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: Theme.roles.input.size
                                    color: step.modelData.state === "pending" ? Theme.dim : Theme.ink
                                }
                                UzMono {
                                    visible: (step.modelData.meta || "") !== ""
                                    Layout.fillWidth: true
                                    text: step.modelData.meta || ""
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: Theme.roles.meta11.size
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    visible: (root.game.error || "") !== "" && !root.running
                    objectName: "startError"
                    Layout.fillWidth: true
                    implicitHeight: errText.implicitHeight + 2 * Theme.space15
                    color: Theme.dangerTint
                    Rectangle { width: 2; height: parent.height; color: Theme.danger }
                    UzLabel {
                        id: errText
                        anchors.fill: parent
                        anchors.margins: Theme.space15
                        text: root.game.error || ""
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.roles.input.size
                        color: Theme.danger
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

        UzButton {
            objectName: "playButton"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: Theme.row44
            kind: "primary"
            busy: root.game.busy === true
            busyOnClick: true
            text: root.running ? qsTr("▶ Resume") : (root.game.playLabel || qsTr("Play"))
            enabled: root.game.canPlay === true && !root.confirming && !(root.running && root.player.quitting)
            onClicked: root.game.running === true ? root.player.resumeGame() : root.player.playSelected()
        }
        // Why Play is off, directly under the button (3c-7).
        UzLabel {
            objectName: "playDisabledReason"
            visible: !root.running && (root.game.playDisabledReason || "") !== "" && root.game.canPlay !== true
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            Layout.topMargin: -Theme.space1
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.accentText
            font.pixelSize: Theme.roles.small.size
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
            spacing: Theme.space1
            UzButton {
                objectName: secondaryRow.offlineError ? "retryConnectionButton"
                            : (root.game.onHub ? "playShareButton" : "uploadToHubButton")
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.minimumWidth: 0
                implicitHeight: Theme.row36
                implicitWidth: 0
                hPadding: 16
                font.pixelSize: Theme.roles.input.size
                text: secondaryRow.offlineError ? qsTr("Retry connection")
                      : (root.game.onHub ? qsTr("Play and share") : qsTr("Upload to server"))
                enabled: secondaryRow.offlineError ? true
                         : (root.game.onHub ? (root.game.canPlay === true && !root.confirming && root.player.sessions.available)
                                            : (root.player.canUpload && root.game.localFile === true && !root.player.upload.active))
                onClicked: secondaryRow.offlineError ? root.player.retryConnection()
                           : (root.game.onHub ? root.player.playAndShareSelected() : root.player.uploadGameToHub(root.game.id))
            }
            UzButton {
                objectName: secondaryRow.offlineError ? "hubSettingsButton" : "removeFromLibraryButton"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.minimumWidth: 0
                implicitHeight: Theme.row36
                implicitWidth: 0
                hPadding: 16
                font.pixelSize: Theme.roles.input.size
                text: secondaryRow.offlineError ? qsTr("Server settings") : qsTr("Remove from library")
                enabled: secondaryRow.offlineError ? true : !root.player.upload.active
                onClicked: secondaryRow.offlineError ? root.player.showSettings() : root.player.removeFromLibrary(root.game.id)
            }
        }
        UzButton {
            objectName: "playShareButton"
            visible: !secondaryRow.visible && (root.player.sessions.available || root.running)
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: Theme.row36
            text: root.game.running === true ? qsTr("Quit game") : qsTr("Play and share Session")
            enabled: (root.running && !root.player.quitting) || (root.game.canPlay === true && !root.confirming)
            onClicked: root.running ? root.player.requestQuit() : root.player.playAndShareSelected()
        }
        UzLabel {
            objectName: "quitNote"
            visible: root.running
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.dim
            font.pixelSize: Theme.roles.small.size
            text: root.player.quitting ? qsTr("Saving and syncing…") : qsTr("Quit saves and syncs to the server first")
        }
        UzLabel {
            objectName: "shareVisibilityHint"
            visible: root.player.sessions.available && root.game.running !== true
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            color: Theme.dim
            font.pixelSize: Theme.roles.small.size
            text: qsTr("Visibility: %1 · change it in the Session panel")
                  .arg(root.player.sessions.visibility === "private" ? qsTr("Private")
                       : root.player.sessions.visibility === "invite_only" ? qsTr("Invite only") : qsTr("Everyone here"))
        }
    }
}
