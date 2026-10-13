import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// 3p: Player settings. Section column (jump list, D2) | one scrolling page with Updates, Hubs, Appearance,
// Diagnostics. Clicking a section scrolls to it and highlights the item. Saved on this device only.
Rectangle {
    id: root
    required property PlayerController player
    readonly property var updates: root.player.updates
    property string currentSection: "updates"
    property string collectMessage: ""
    property string collectedPath: ""
    property string editingHubId: ""
    color: Theme.bg

    readonly property var savedHubs: root.player.hubs.filter(function (h) { return h.saved })
    readonly property string activeHubName: {
        for (let i = 0; i < root.savedHubs.length; ++i) {
            if (root.savedHubs[i].connected) {
                return root.savedHubs[i].name
            }
        }
        return ""
    }

    // "Plugin settings" / "Notice settings…" of a plugin surface: scroll to Plugins, highlight the plugin's row.
    Connections {
        target: root.player
        function onPluginSettingsRequested(pluginId: string) {
            Qt.callLater(function () {
                root.jumpTo("plugins")
                secPlugins.highlightId = pluginId
            })
        }
    }

    function sectionItem(name: string): Item {
        switch (name) {
        case "hubs": return secHubs
        case "appearance": return secAppearance
        case "library": return secLibrary
        case "storage": return secStorage
        case "plugins": return secPlugins
        case "diagnostics": return secDiagnostics
        default: return secUpdates
        }
    }
    function jumpTo(name: string) {
        root.currentSection = name
        const target = Math.max(0, Math.min(sectionItem(name).y + content.y - 20, flick.contentHeight - flick.height))
        scrollAnim.stop()
        scrollAnim.to = target
        scrollAnim.start()
    }
    function syncCurrentFromScroll() {
        if (scrollAnim.running) {
            return
        }
        const probe = flick.contentY + 80
        let best = "updates"
        for (const n of ["updates", "hubs", "appearance", "library", "storage", "plugins", "diagnostics"]) {
            if (sectionItem(n).y + content.y <= probe) {
                best = n
            }
        }
        if (flick.contentY + flick.height >= flick.contentHeight - 2) {
            best = "diagnostics"
        }
        root.currentSection = best
    }
    function libraryNavText() {
        const n = root.player.libraryFolders.length
        if (n === 0) return qsTr("No folders")
        const g = root.player.libraryGameCount
        return Theme.plural(n, qsTr("folder"), qsTr("folders")) + " · " + Theme.plural(g, qsTr("game"), qsTr("games"))
    }
    function updatesLine(): string {
        const u = root.updates
        if (u.updateAvailable) {
            return qsTr("▲ Update available · %1").arg(u.availableVersion)
        }
        switch (u.state) {
        case "checking": return qsTr("Checking…")
        case "up_to_date": return qsTr("Up to date")
        case "error": return qsTr("Check failed")
        case "disabled": return qsTr("Off")
        default: return u.currentVersion
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.sidebarWidth
            player: root.player
        }

        ShellColumn {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.columnWidth
            title: qsTr("Settings")

            Repeater {
                model: [
                    { id: "updates", name: qsTr("Updates") },
                    { id: "hubs", name: qsTr("Hubs") },
                    { id: "appearance", name: qsTr("Appearance") },
                    { id: "library", name: qsTr("Library folders") },
                    { id: "storage", name: qsTr("ROM cache") },
                    { id: "plugins", name: qsTr("Plugins") },
                    { id: "diagnostics", name: qsTr("Diagnostics") }
                ]
                delegate: ColumnItem {
                    id: sec
                    required property var modelData
                    objectName: "section_" + modelData.id
                    selected: root.currentSection === modelData.id
                    onClicked: root.jumpTo(sec.modelData.id)
                    FbLabel { text: sec.modelData.name; font.weight: Font.DemiBold }
                    FbLabel {
                        objectName: "sectionStatus_" + sec.modelData.id
                        Layout.fillWidth: true
                        font.pixelSize: Theme.fontMeta
                        elide: Text.ElideRight
                        color: sec.modelData.id === "updates" && root.updates.updateAvailable ? Theme.accent : Theme.textMuted
                        text: sec.modelData.id === "updates" ? root.updatesLine()
                              : sec.modelData.id === "hubs" ? (root.activeHubName !== ""
                                                               ? qsTr("%1 saved · %2 active").arg(root.savedHubs.length).arg(root.activeHubName)
                                                               : qsTr("%1 saved").arg(root.savedHubs.length))
                              : sec.modelData.id === "appearance" ? (root.player.appearance === "light" ? qsTr("Light")
                                                                     : root.player.appearance === "system" ? qsTr("System") : qsTr("Dark"))
                              : sec.modelData.id === "library" ? root.libraryNavText()
                              : sec.modelData.id === "storage" ? qsTr("%1 of %2").arg(root.player.romCache.usedText).arg(root.player.romCache.limitText)
                              : sec.modelData.id === "plugins" ? root.player.plugins.summary
                              : qsTr("Logs and support")
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        Flickable {
            id: flick
            objectName: "settingsScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: content.implicitHeight + 72
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }
            onContentYChanged: root.syncCurrentFromScroll()
            NumberAnimation { id: scrollAnim; target: flick; property: "contentY"; duration: Theme.durList; easing.type: Easing.OutCubic }

            ColumnLayout {
                id: content
                x: 36
                y: 28
                width: Math.min(Theme.contentMaxWidth, flick.width - 72)   // left-aligned, never wider than 880 (P7); descriptions are capped per row
                // Hub card and update card sit on the label edge (14 in) and end where the controls end (72 in) on wide pages.
                readonly property bool wide: width >= Theme.settingsNarrowBelow
                spacing: 32

                ColumnLayout {
                    spacing: 2
                    FbLabel { text: qsTr("Settings"); font.pixelSize: Theme.fontPage; font.weight: Font.DemiBold; font.letterSpacing: -0.4 }
                    FbLabel {
                        text: qsTr("Saved on this device only")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                    }
                }

                // ------------------------------------------------------------------ UPDATES
                ColumnLayout {
                    id: secUpdates
                    objectName: "updatesSection"
                    Layout.fillWidth: true
                    spacing: Theme.space14

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Eyebrow { text: qsTr("Updates"); Layout.fillWidth: true }
                        FbPill {
                            objectName: "updatesPill"
                            visible: root.updates.state === "up_to_date" && !root.updates.updateAvailable
                            tone: "ok"
                            text: qsTr("✓ Up to date · %1").arg(root.updates.currentVersion)
                        }
                    }

                    // Update card
                    Rectangle {
                        objectName: "updateCard"
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.settingsDotSlot
                        Layout.rightMargin: content.wide ? Theme.settingsResetWidth : 0   // ends where the controls end (P5)
                        implicitHeight: cardCol.implicitHeight + 36
                        radius: Theme.radius10
                        color: Theme.surface
                        border.width: 1
                        border.color: root.updates.updateAvailable ? Theme.accentBorder : Theme.borderCard

                        ColumnLayout {
                            id: cardCol
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: Theme.space10

                            RowLayout {
                                spacing: Theme.space10
                                FbLabel {
                                    objectName: "updateCardTitle"
                                    text: root.updates.updateAvailable ? qsTr("▲ Update available")
                                          : (root.updates.state === "checking" ? qsTr("Checking for updates…")
                                             : root.updates.effectiveChannel === "off" ? qsTr("Updates are off")
                                             : qsTr("Player %1 is up to date").arg(root.updates.currentVersion))
                                    font.pixelSize: Theme.fontSection
                                    font.weight: Font.DemiBold
                                    color: root.updates.updateAvailable ? Theme.warn : Theme.text
                                }
                                FbMono {
                                    objectName: "updatesAvailableVersion"
                                    visible: root.updates.updateAvailable
                                    text: root.updates.availableVersion
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSmall
                                }
                                Item { Layout.fillWidth: true }
                                FbSpinner { visible: root.updates.checking; size: 14 }
                            }
                            FbLabel {
                                objectName: "updatesStatus"
                                Layout.fillWidth: true
                                text: root.updates.updateAvailable
                                      ? qsTr("You have %1. Installs on the next start, never during a game.").arg(root.updates.currentVersion)
                                      : (root.updates.state === "up_to_date" || root.updates.state === "idle") && root.updates.effectiveChannel !== "off"
                                        ? qsTr("%1 channel · checked %2").arg(root.updates.effectiveChannel === "beta" ? qsTr("Beta") : qsTr("Stable")).arg(root.updates.lastCheckText)
                                      : root.updates.state === "idle" && root.updates.effectiveChannel === "off"
                                        ? qsTr("Choose Stable or Beta to check for updates.")
                                      : root.updates.statusText
                                wrapMode: Text.WordWrap
                                font.pixelSize: Theme.fontSmall
                                color: root.updates.state === "error" ? Theme.toneColor("error") : Theme.textSecondary
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: 6
                                radius: 3
                                visible: root.updates.state === "downloading"
                                color: Theme.borderRow
                                Rectangle {
                                    width: parent.width * root.updates.progress
                                    height: parent.height
                                    radius: 3
                                    color: Theme.accent
                                    Behavior on width { NumberAnimation { duration: Theme.durFast } }
                                }
                            }
                            RowLayout {
                                spacing: Theme.space10
                                visible: root.updates.notesUrl !== "" || root.updates.canInstall
                                FbButton {
                                    objectName: "updatesNotes"
                                    visible: root.updates.notesUrl !== ""
                                    implicitHeight: 36
                                    text: qsTr("What's new")
                                    onClicked: root.updates.openNotes()
                                }
                                FbButton {
                                    objectName: "updatesInstall"
                                    visible: root.updates.canInstall
                                    implicitHeight: 36
                                    kind: "primary"
                                    busyOnClick: true
                                    text: root.updates.state === "ready" ? qsTr("Restart and update") : qsTr("Install and restart")
                                    onClicked: root.updates.install()
                                }
                            }
                        }
                    }

                    // Rows
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 0
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderRow }

                        SettingsRow {
                            objectName: "updateChannelRow"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            resetMode: "none"
                            label: qsTr("Update channel")
                            descriptionName: "updateChannelHint"
                            description: root.updates.compiledChannel === "beta" ? qsTr("Stable: tested releases. Beta: earlier, less tested. This build is a beta.")
                                                                 : qsTr("Stable: tested releases. Beta: earlier, less tested.")
                            ctrl: "segment"
                            controlName: "updateChannelSegment"
                            values: [
                                { value: "off", label: qsTr("Off"), name: "updateChannelOff" },
                                { value: "stable", label: qsTr("Stable"), name: "updateChannelStable" },
                                { value: "beta", label: qsTr("Beta"), name: "updateChannelBeta" }
                            ]
                            current: root.updates.channelSetting === "default" ? root.updates.effectiveChannel : root.updates.channelSetting
                            onValuePicked: value => root.updates.setChannel(value)
                        }

                        SettingsRow {
                            objectName: "updateAutoInstallRow"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            resetMode: "none"
                            label: qsTr("Install updates automatically")
                            description: root.updates.autoInstallAvailable
                                         ? qsTr("Downloaded in the background, applied on the next start.")
                                         : qsTr("Stable updates are only installed after you confirm.")
                            ctrl: "toggle"
                            toggleName: "updateAutoInstallToggle"
                            controlEnabled: root.updates.autoInstallAvailable
                            checked: root.updates.autoInstall
                            onToggled: on => root.updates.setAutoInstall(on)
                        }

                        SettingsRow {
                            objectName: "updatesLastCheckRow"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            resetMode: "none"
                            label: qsTr("Last checked")
                            descriptionName: "updatesLastCheck"
                            description: root.updates.cadenceText !== ""
                                         ? qsTr("%1 · %2").arg(root.updates.lastCheckText).arg(root.updates.cadenceText)
                                         : root.updates.lastCheckText
                            ctrl: "buttons"
                            buttons: [{ name: "updatesCheckNow", text: qsTr("Check now"), busy: root.updates.checking, busyOnClick: true,
                                        enabled: !root.updates.checking && root.updates.state !== "downloading" }]
                            onButtonClicked: root.updates.checkNow()
                        }

                        SettingsRow {
                            objectName: "versionRow"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            resetMode: "none"
                            label: qsTr("Version")
                            description: root.player.platformText
                            ctrl: "value"
                            controlName: "updatesVersion"
                            valueText: root.updates.currentVersion
                        }
                    }
                }

                // ------------------------------------------------------------------ HUBS
                ColumnLayout {
                    id: secHubs
                    objectName: "hubsSection"
                    Layout.fillWidth: true
                    spacing: Theme.space12

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.rightMargin: content.wide ? Theme.settingsResetWidth - 8 : 0   // "+ Add hub" ends where the controls end (P1)
                        Eyebrow { objectName: "hubsEyebrow"; text: qsTr("Hubs · %1").arg(root.savedHubs.length); Layout.fillWidth: true }
                        FbButton {
                            objectName: "settingsAddHubButton"
                            kind: "link"
                            text: qsTr("+ Add hub")
                            onClicked: root.player.switchHub()
                        }
                    }

                    // Own row (not in the header): a long label must never widen the settings column.
                    FbButton {
                        objectName: "settingsLocalHubButton"
                        visible: root.player.localHub.available && !root.player.localHub.onThisPc
                        Layout.alignment: Qt.AlignLeft
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: content.width - Theme.settingsDotSlot
                        kind: "link"
                        text: qsTr("Set up a Hub on this PC")
                        onClicked: { root.player.switchHub(); root.player.localHub.start() }
                    }

                    LocalHubCard {
                        objectName: "localHubSettingsCard"
                        player: root.player
                        Layout.leftMargin: Theme.settingsDotSlot
                        Layout.rightMargin: content.wide ? Theme.settingsResetWidth : 0
                    }

                    // Stable model keyed by hub id: hubsChanged updates rows in place instead of rebuilding every delegate
                    // (a JS-array model recreates the nested Layouts on each change; Qt 6.4 crashes in
                    // QQuickGridLayoutBase::rearrange when that happens while the layouts are being polished).
                    ListModel {
                        id: hubsModel
                        dynamicRoles: true
                        function sync(list) {
                            for (var i = 0; i < list.length; i++) {
                                var found = -1
                                for (var j = i; j < count; j++) {
                                    if (get(j).hubId === list[i].hubId) { found = j; break }
                                }
                                if (found < 0) insert(i, list[i])
                                else {
                                    if (found !== i) move(found, i, 1)
                                    set(i, list[i])
                                }
                            }
                            if (count > list.length) remove(list.length, count - list.length)
                        }
                    }
                    Connections {
                        target: root.player
                        function onHubsChanged() { hubsModel.sync(root.player.hubs) }
                    }
                    Component.onCompleted: hubsModel.sync(root.player.hubs)

                    Repeater {
                        model: hubsModel
                        delegate: Rectangle {
                            id: hubRow
                            required property var model
                            readonly property var modelData: model
                            required property int index
                            property bool confirmRemove: false
                            readonly property bool editing: root.editingHubId === modelData.hubId && modelData.hubId !== ""
                            readonly property bool lost: modelData.pairingLost === true && !modelData.connected
                            visible: modelData.saved
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.leftMargin: Theme.settingsDotSlot
                            Layout.rightMargin: content.wide ? Theme.settingsResetWidth : 0
                            Layout.preferredHeight: visible ? rowCol.implicitHeight + 28 : 0
                            radius: Theme.radius10
                            color: Theme.surface
                            border.width: modelData.connected ? 1.5 : 1
                            border.color: modelData.connected ? Theme.accent : Theme.borderCard

                            ColumnLayout {
                                id: rowCol
                                anchors.fill: parent
                                anchors.margins: 14
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                spacing: Theme.space12
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 24
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        spacing: 2
                                        RowLayout {
                                            spacing: Theme.space8
                                            FbLabel {
                                                objectName: "hubName_" + hubRow.modelData.hubId
                                                text: hubRow.modelData.name
                                                font.pixelSize: Theme.fontSection
                                                font.weight: Font.Medium
                                                elide: Text.ElideRight
                                                Layout.maximumWidth: 280
                                            }
                                            FbPill {
                                                objectName: "hubState_" + hubRow.modelData.hubId
                                                small: true
                                                tone: hubRow.modelData.connected ? "ok" : (hubRow.lost ? "amber" : (hubRow.modelData.tone === "neutral" ? "neutral" : hubRow.modelData.tone))
                                                text: hubRow.modelData.connected ? qsTr("● Connected · active")
                                                      : hubRow.lost ? qsTr("Pairing lost")
                                                      : (hubRow.modelData.status === "idle" ? qsTr("Not connected") : hubRow.modelData.statusText)
                                            }
                                        }
                                        FbMono {
                                            Layout.fillWidth: true
                                            text: hubRow.modelData.detail
                                            elide: Text.ElideRight
                                            HoverHandler { id: detailHover }
                                            ToolTip.visible: detailHover.hovered && (hubRow.modelData.lastConnectedFull || "") !== ""
                                            ToolTip.text: hubRow.modelData.lastConnectedFull || ""
                                            ToolTip.delay: 400
                                        }
                                    }
                                    // Control column (280, right edge = the controls of the rows): [Switch | Pair again] [Edit] [⋯].
                                    RowLayout {
                                        Layout.preferredWidth: content.wide ? Theme.settingsControlWidth - 16 : -1
                                        Layout.alignment: Qt.AlignVCenter
                                        spacing: 6
                                        visible: !hubRow.confirmRemove
                                        Item { Layout.fillWidth: true }
                                        FbButton {
                                            objectName: "hubSwitch_" + hubRow.modelData.hubId
                                            visible: !hubRow.modelData.connected && !hubRow.lost
                                            implicitHeight: 32
                                            hPadding: 24
                                            busy: hubRow.modelData.status === "connecting"
                                            busyOnClick: true
                                            text: qsTr("Switch")
                                            enabled: hubRow.modelData.status !== "connecting"
                                            onClicked: root.player.switchToHub(hubRow.modelData.hubId)
                                        }
                                        FbButton {
                                            objectName: "hubPairAgain_" + hubRow.modelData.hubId
                                            visible: hubRow.lost
                                            implicitHeight: 32
                                            hPadding: 24
                                            busyOnClick: true
                                            text: qsTr("Pair again")
                                            onClicked: root.player.switchToHub(hubRow.modelData.hubId)
                                        }
                                        FbButton {
                                            objectName: "hubEdit_" + hubRow.modelData.hubId
                                            implicitHeight: 32
                                            hPadding: 24
                                            text: qsTr("Edit")
                                            onClicked: {
                                                hubEditAddress.text = hubRow.modelData.host + ":" + hubRow.modelData.port
                                                hubEditError.extra = ""
                                                root.editingHubId = hubRow.editing ? "" : hubRow.modelData.hubId
                                            }
                                        }
                                        MoreMenu {
                                            objectName: "hubMenu_" + hubRow.modelData.hubId
                                            tooltip: qsTr("More actions for %1").arg(hubRow.modelData.name)
                                            items: [{ name: "remove", label: qsTr("Remove Hub…"), danger: true, divider: false }]
                                            onActivated: name => { if (name === "remove") hubRow.confirmRemove = true }
                                        }
                                    }
                                }

                                // Connect automatically: part of the active Hub's card (3p-3).
                                RowLayout {
                                    visible: hubRow.modelData.connected && !hubRow.confirmRemove
                                    Layout.fillWidth: true
                                    spacing: 24
                                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 0; visible: false }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        spacing: 2
                                        FbLabel { text: qsTr("Connect automatically on startup"); font.pixelSize: Theme.fontBody }
                                        FbLabel { text: qsTr("Uses this Hub when the Player starts."); color: Theme.textMuted; font.pixelSize: Theme.fontMeta }
                                    }
                                    Item {
                                        Layout.preferredWidth: content.wide ? Theme.settingsControlWidth - 16 : 40
                                        Layout.preferredHeight: 28
                                        FbToggle {
                                            id: autoToggle
                                            objectName: "settingsAutoConnectToggle"
                                            anchors.right: parent.right
                                            checked: root.player.autoConnect
                                            onToggled: root.player.autoConnect = checked
                                            Accessible.name: qsTr("Connect automatically on startup")
                                        }
                                    }
                                }

                                // Inline edit: one field "host:port" (hub id, fingerprint, sign-in and user stay).
                                ColumnLayout {
                                    id: editBox
                                    objectName: "hubEditRow_" + hubRow.modelData.hubId
                                    visible: hubRow.editing && !hubRow.confirmRemove
                                    Layout.fillWidth: true
                                    spacing: Theme.space8
                                    // "host" or "host:port"; the port defaults to 8443 when left out.
                                    readonly property var parts: {
                                        const t = hubEditAddress.text.trim()
                                        const k = t.lastIndexOf(":")
                                        if (k < 0 || t.indexOf("://") >= 0 && k === t.indexOf("://")) return { host: t, port: "8443" }
                                        return { host: t.slice(0, k), port: t.slice(k + 1) }
                                    }
                                    readonly property var messages: root.player.validateHubAddress(parts.host, parts.port)
                                    readonly property bool valid: messages.length === 0 && hubEditAddress.text.trim() !== ""
                                    readonly property bool changed: hubEditAddress.text.trim() !== hubRow.modelData.host + ":" + hubRow.modelData.port

                                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderRow }
                                    Eyebrow { text: qsTr("Edit · %1").arg(hubRow.modelData.name) }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.space10
                                        FbLabel { text: qsTr("Address"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall; Layout.preferredWidth: 70 }
                                        FbField {
                                            id: hubEditAddress
                                            objectName: "hubEditAddress_" + hubRow.modelData.hubId
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            implicitHeight: 36
                                            invalid: !editBox.valid && hubEditAddress.text.trim() !== ""
                                            placeholderText: qsTr("hub.example.com:8443")
                                            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                                            onAccepted: hubEditSave.clicked()
                                        }
                                        FbButton {
                                            objectName: "hubEditCancel_" + hubRow.modelData.hubId
                                            implicitHeight: 36
                                            text: qsTr("Cancel")
                                            onClicked: root.editingHubId = ""
                                        }
                                        FbButton {
                                            id: hubEditSave
                                            objectName: "hubEditSave_" + hubRow.modelData.hubId
                                            implicitHeight: 36
                                            kind: "primary"
                                            busyOnClick: true
                                            text: qsTr("Save")
                                            enabled: editBox.valid && editBox.changed
                                            onClicked: {
                                                if (!editBox.valid) {
                                                    return
                                                }
                                                if (root.player.editHub(hubRow.modelData.hubId, editBox.parts.host, editBox.parts.port)) {
                                                    hubEditError.extra = ""
                                                    root.editingHubId = ""
                                                } else {
                                                    hubEditError.extra = qsTr("Another saved hub already uses this address")
                                                }
                                            }
                                        }
                                    }
                                    FbLabel {
                                        id: hubEditError
                                        objectName: "hubEditError_" + hubRow.modelData.hubId
                                        property string extra: ""
                                        Layout.fillWidth: true
                                        visible: text !== "" && hubEditAddress.text.trim() !== ""
                                        text: editBox.messages.length > 0 ? editBox.messages.join(" · ") : extra
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: Theme.fontMeta
                                        color: Theme.error
                                    }
                                    FbLabel {
                                        objectName: "hubEditHint_" + hubRow.modelData.hubId
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: Theme.fontMeta
                                        color: Theme.textMuted
                                        text: qsTr("host:port, without https://. If the Hub shows a different certificate, the Player asks you to compare it.")
                                    }
                                }

                                RowLayout {
                                    objectName: "hubRemoveConfirm_" + hubRow.modelData.hubId
                                    Layout.fillWidth: true
                                    spacing: Theme.space10
                                    visible: hubRow.confirmRemove
                                    FbLabel {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: Theme.fontMeta
                                        color: Theme.text
                                        text: hubRow.modelData.connected
                                              ? qsTr("Remove %1 from the Player? Running Sessions end and you return to the connection screen. Saves on the Hub are not deleted.").arg(hubRow.modelData.name)
                                              : qsTr("Remove %1 from the Player? Saves on the Hub are not deleted.").arg(hubRow.modelData.name)
                                    }
                                    FbButton {
                                        objectName: "hubRemoveCancel_" + hubRow.modelData.hubId
                                        implicitHeight: 32
                                        text: qsTr("Cancel")
                                        onClicked: hubRow.confirmRemove = false
                                    }
                                    FbButton {
                                        objectName: "hubRemoveConfirmButton_" + hubRow.modelData.hubId
                                        implicitHeight: 32
                                        kind: "primary"
                                        busyOnClick: true
                                        text: qsTr("Remove")
                                        onClicked: root.player.removeHub(hubRow.modelData.hubId)
                                    }
                                }
                            }
                        }
                    }

                    FbLabel {
                        objectName: "hubsEmpty"
                        visible: root.player.hubs.length === 0
                        Layout.fillWidth: true
                        text: qsTr("No Hub saved yet.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                    }
                }

                // ------------------------------------------------------------------ APPEARANCE
                ColumnLayout {
                    id: secAppearance
                    objectName: "appearanceSection"
                    Layout.fillWidth: true
                    spacing: Theme.space12
                    Eyebrow { text: qsTr("Appearance") }
                    SettingsRow {
                        objectName: "themeRow"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        resetMode: "none"
                        label: qsTr("Theme")
                        description: qsTr("The game view always stays dark.")
                        ctrl: "segment"
                        controlName: "appearanceSegment"
                        values: [
                            { value: "dark", label: qsTr("Dark"), name: "appearanceDark" },
                            { value: "light", label: qsTr("Light"), name: "appearanceLight" },
                            { value: "system", label: qsTr("System"), name: "appearanceSystem" }
                        ]
                        current: root.player.appearance
                        onValuePicked: value => root.player.appearance = value
                    }
                }

                // ------------------------------------------------------------------ LIBRARY FOLDERS
                LibraryFoldersSection {
                    id: secLibrary
                    objectName: "libraryFoldersSection"
                    Layout.fillWidth: true
                    player: root.player
                }

                // ------------------------------------------------------------------ ROM CACHE
                ColumnLayout {
                    id: secStorage
                    objectName: "storageSection"
                    Layout.fillWidth: true
                    spacing: Theme.space12
                    property bool confirmClear: false
                    property string clearResult: ""
                    Eyebrow { text: qsTr("ROM cache") }
                    SettingsRow {
                        objectName: "romCacheLimitRow"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        resetMode: "none"
                        label: qsTr("Size limit")
                        meta: qsTr("Used: %1").arg(root.player.romCache.usedText)
                        descriptionName: "romCacheLimitHint"
                        description: qsTr("Downloaded games are kept on this device. When the limit is exceeded, the least recently played games are removed; the running game and downloads in progress are never removed. They are downloaded again when needed.")
                        ctrl: "select"
                        controlName: "romCacheLimitSelect"
                        values: root.player.romCache.limitOptions
                        current: String(root.player.romCache.limitBytes)
                        onValuePicked: value => root.player.setRomCacheLimit(Number(value))
                    }
                    SettingsRow {
                        objectName: "romCacheClearRow"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        resetMode: "none"
                        label: qsTr("Clear ROM cache")
                        descriptionName: "romCacheClearHint"
                        description: secStorage.clearResult !== "" ? secStorage.clearResult
                                     : qsTr("Removes all downloaded games except the running one. Saves are not affected.")
                        ctrl: "buttons"
                        buttons: [{ name: "romCacheClear", text: qsTr("Clear…"), enabled: root.player.romCache.clearableBytes > 0 && !secStorage.confirmClear }]
                        onButtonClicked: { secStorage.clearResult = ""; secStorage.confirmClear = true }
                    }
                    RowLayout {
                        objectName: "romCacheClearConfirm"
                        Layout.fillWidth: true
                        spacing: Theme.space10
                        visible: secStorage.confirmClear
                        FbLabel {
                            objectName: "romCacheClearConfirmText"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.fontMeta
                            color: Theme.text
                            text: qsTr("Remove cached ROMs and free %1? Games are downloaded again when you play them. Saves are not deleted.").arg(root.player.romCache.clearableText)
                        }
                        FbButton {
                            objectName: "romCacheClearCancel"
                            implicitHeight: 32
                            text: qsTr("Cancel")
                            onClicked: secStorage.confirmClear = false
                        }
                        FbButton {
                            objectName: "romCacheClearConfirmButton"
                            implicitHeight: 32
                            kind: "primary"
                            text: qsTr("Clear cache")
                            onClicked: {
                                const r = root.player.clearRomCache()
                                secStorage.confirmClear = false
                                secStorage.clearResult = qsTr("Freed %1 (%n file(s) removed).", "", r.removedFiles).arg(r.freedText)
                            }
                        }
                    }
                }

                // ------------------------------------------------------------------ PLUGINS
                PluginsSection {
                    id: secPlugins
                    objectName: "pluginsSection"
                    Layout.fillWidth: true
                    player: root.player
                    hubName: root.activeHubName
                }

                // ------------------------------------------------------------------ DIAGNOSTICS
                ColumnLayout {
                    id: secDiagnostics
                    objectName: "diagnosticsSection"
                    Layout.fillWidth: true
                    spacing: Theme.space12
                    Eyebrow { text: qsTr("Diagnostics") }
                    SettingsRow {
                        objectName: "logFileRow"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        resetMode: "none"
                        label: qsTr("Log file")
                        descriptionName: "logFilePath"
                        description: root.player.logFile !== "" ? root.player.logFile : qsTr("File logging is not active")
                        ctrl: "buttons"
                        buttons: [
                            { name: "logCopyPath", text: copied.running ? qsTr("Copied") : qsTr("Copy path"), visible: root.player.logFile !== "" },
                            { name: "logFolderOpen", text: qsTr("Open folder"), visible: root.player.logFile !== "" }
                        ]
                        onButtonClicked: name => {
                            if (name === "logCopyPath") {
                                clip.text = root.player.logFile
                                clip.selectAll()
                                clip.copy()
                                copied.restart()
                            } else if (name === "logFolderOpen") {
                                root.player.openLogFolder()
                            }
                        }
                        Timer { id: copied; interval: 1500 }
                    }
                    TextEdit { id: clip; visible: false }
                    SettingsRow {
                        objectName: "collectLogsRow"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        resetMode: "none"
                        label: qsTr("Collect logs")
                        descriptionName: "collectLogsStatus"
                        description: root.collectMessage !== "" ? root.collectMessage
                            : root.player.collectingDiagnostics ? qsTr("Collecting…")
                            : qsTr("Packs the logs and system information into one zip file to send along with a bug report. Contains IP addresses and host names, no passwords or tokens.")
                        ctrl: "buttons"
                        buttons: [
                            { name: "collectLogs", text: qsTr("Collect logs…"), visible: !root.player.collectingDiagnostics },
                            { name: "collectShow", text: qsTr("Show in folder"), visible: root.collectedPath !== "" }
                        ]
                        onButtonClicked: name => {
                            if (name === "collectLogs") {
                                collectDialog.open()
                            } else if (name === "collectShow") {
                                root.player.showInFolder(root.collectedPath)
                            }
                        }
                    }
                    Loader {
                        id: collectDialog
                        active: false
                        source: "CollectLogsDialog.qml"
                        function open() {
                            if (item) {
                                item.prepare()
                                item.open()
                            } else {
                                active = true
                            }
                        }
                        onLoaded: {
                            item.player = root.player
                            item.prepare()
                            item.open()
                        }
                        onStatusChanged: if (status === Loader.Error) {
                            root.collectMessage = qsTr("The file dialog is not available in this installation.")
                        }
                    }
                    Connections {
                        target: root.player
                        function onDiagnosticsCollected(ok, path, message) {
                            root.collectedPath = ok ? path : ""
                            root.collectMessage = ok ? qsTr("Saved %1").arg(path) : qsTr("Could not save the bundle: %1").arg(message)
                        }
                    }
                }
            }
        }
    }
}
