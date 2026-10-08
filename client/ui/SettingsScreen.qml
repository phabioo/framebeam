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

    function sectionItem(name: string): Item {
        switch (name) {
        case "hubs": return secHubs
        case "appearance": return secAppearance
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
        for (const n of ["updates", "hubs", "appearance", "diagnostics"]) {
            if (sectionItem(n).y + content.y <= probe) {
                best = n
            }
        }
        if (flick.contentY + flick.height >= flick.contentHeight - 2) {
            best = "diagnostics"
        }
        root.currentSection = best
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
                              : qsTr("Logs and support")
                    }
                }
            }

            Item { Layout.fillHeight: true }
            FbMono {
                objectName: "versionFooter"
                Layout.fillWidth: true
                Layout.leftMargin: 4
                text: qsTr("Player %1").arg(root.player.playerVersion)
                font.pixelSize: Theme.fontMono
                color: Theme.textMeta
                elide: Text.ElideRight
            }
            FbMono {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                text: root.player.platformText
                font.pixelSize: Theme.fontMono
                color: Theme.textMeta
                elide: Text.ElideRight
            }
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
                width: Math.min(Theme.settingsContentMax, flick.width - 72)
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
                                          : (root.updates.state === "checking" ? qsTr("Checking for updates…") : qsTr("Player is up to date"))
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
                            meta: qsTr("Channel: %1").arg(root.updates.effectiveChannel)
                            descriptionName: "updateChannelHint"
                            description: root.updates.effectiveChannel === "beta"
                                         ? qsTr("Pre-release builds from every change on main. May contain bugs.")
                                         : qsTr("Tested releases. Recommended for everyday use.")
                            ctrl: "segment"
                            controlName: "updateChannelSegment"
                            values: [
                                { value: "stable", label: qsTr("Stable"), name: "updateChannelStable" },
                                { value: "beta", label: root.updates.compiledChannel === "beta" ? qsTr("Beta · this build") : qsTr("Beta"), name: "updateChannelBeta" }
                            ]
                            current: root.updates.channelSetting === "default" ? (root.updates.effectiveChannel !== "off" ? root.updates.effectiveChannel : root.updates.compiledChannel) : root.updates.channelSetting
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
                        Eyebrow { text: qsTr("Hubs"); Layout.fillWidth: true }
                        FbButton {
                            objectName: "settingsAddHubButton"
                            kind: "link"
                            text: qsTr("+ Add hub")
                            onClicked: root.player.switchHub()
                        }
                    }

                    Repeater {
                        model: root.player.hubs
                        delegate: Rectangle {
                            id: hubRow
                            required property var modelData
                            required property int index
                            property bool confirmRemove: false
                            readonly property bool editing: root.editingHubId === modelData.hubId && modelData.hubId !== ""
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
                                spacing: Theme.space10
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.space12
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        spacing: 4
                                        RowLayout {
                                            spacing: Theme.space10
                                            FbLabel {
                                                objectName: "hubName_" + hubRow.modelData.hubId
                                                text: hubRow.modelData.name
                                                font.pixelSize: Theme.fontSection
                                                font.weight: Font.DemiBold
                                                elide: Text.ElideRight
                                                Layout.maximumWidth: 280
                                            }
                                            FbPill {
                                                objectName: "hubState_" + hubRow.modelData.hubId
                                                tone: hubRow.modelData.connected ? "ok" : (hubRow.modelData.tone === "neutral" ? "neutral" : hubRow.modelData.tone)
                                                text: hubRow.modelData.connected ? qsTr("Connected") : (hubRow.modelData.status === "idle" ? qsTr("Not connected") : hubRow.modelData.statusText)
                                            }
                                        }
                                        FbMono { Layout.fillWidth: true; text: hubRow.modelData.detail; elide: Text.ElideRight }
                                    }
                                    RowLayout {
                                        spacing: Theme.space8
                                        visible: !hubRow.confirmRemove
                                        FbLabel {
                                            objectName: "hubCurrent_" + hubRow.modelData.hubId
                                            visible: hubRow.modelData.connected
                                            text: qsTr("Active hub")
                                            font.pixelSize: Theme.fontSmall
                                            font.weight: Font.Medium
                                            color: Theme.accent
                                        }
                                        FbButton {
                                            objectName: "hubSwitch_" + hubRow.modelData.hubId
                                            visible: !hubRow.modelData.connected
                                            implicitHeight: 32
                                            busy: hubRow.modelData.status === "connecting"
                                            busyOnClick: true
                                            text: qsTr("Switch")
                                            enabled: hubRow.modelData.status !== "connecting"
                                            onClicked: root.player.switchToHub(hubRow.modelData.hubId)
                                        }
                                        FbButton {
                                            objectName: "hubEdit_" + hubRow.modelData.hubId
                                            kind: "link"
                                            text: qsTr("Edit")
                                            onClicked: {
                                                hubEditHost.text = hubRow.modelData.host
                                                hubEditPort.text = hubRow.modelData.port
                                                hubEditError.extra = ""
                                                root.editingHubId = hubRow.editing ? "" : hubRow.modelData.hubId
                                            }
                                        }
                                        FbButton {
                                            objectName: "hubRemove_" + hubRow.modelData.hubId
                                            kind: "link"
                                            text: qsTr("Remove")
                                            onClicked: hubRow.confirmRemove = true
                                        }
                                    }
                                }

                                // Inline edit: address and port only (hub id, fingerprint, sign-in and user stay).
                                ColumnLayout {
                                    id: editBox
                                    objectName: "hubEditRow_" + hubRow.modelData.hubId
                                    visible: hubRow.editing && !hubRow.confirmRemove
                                    Layout.fillWidth: true
                                    spacing: Theme.space8
                                    readonly property var messages: root.player.validateHubAddress(hubEditHost.text, hubEditPort.text)
                                    readonly property bool hostValid: root.player.validateHubAddress(hubEditHost.text, "8443").length === 0
                                    readonly property bool portValid: root.player.validateHubAddress("example.com", hubEditPort.text).length === 0
                                    readonly property bool valid: hostValid && portValid
                                    readonly property bool changed: hubEditHost.text.trim() !== hubRow.modelData.host || hubEditPort.text.trim() !== hubRow.modelData.port

                                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderRow }
                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: 5
                                        columnSpacing: Theme.space10
                                        rowSpacing: Theme.space8
                                        FbLabel { text: qsTr("Address"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                                        FbField {
                                            id: hubEditHost
                                            objectName: "hubEditHost_" + hubRow.modelData.hubId
                                            Layout.fillWidth: true
                                            implicitHeight: 36
                                            invalid: !editBox.hostValid
                                            placeholderText: qsTr("hub.example.com")
                                            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                                            onAccepted: hubEditSave.clicked()
                                        }
                                        FbLabel { text: qsTr("Port"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                                        FbField {
                                            id: hubEditPort
                                            objectName: "hubEditPort_" + hubRow.modelData.hubId
                                            Layout.preferredWidth: 96
                                            implicitHeight: 36
                                            invalid: !editBox.portValid
                                            inputMethodHints: Qt.ImhDigitsOnly
                                            onAccepted: hubEditSave.clicked()
                                        }
                                        RowLayout {
                                            spacing: Theme.space8
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
                                                    if (root.player.editHub(hubRow.modelData.hubId, hubEditHost.text, hubEditPort.text)) {
                                                        hubEditError.extra = ""
                                                        root.editingHubId = ""
                                                    } else {
                                                        hubEditError.extra = qsTr("Another saved hub already uses this address")
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    FbLabel {
                                        id: hubEditError
                                        objectName: "hubEditError_" + hubRow.modelData.hubId
                                        property string extra: ""
                                        Layout.fillWidth: true
                                        visible: text !== ""
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
                                        text: qsTr("Certificate fingerprint stays the same. If the hub at the new address shows a different certificate, the Player stops and asks you to compare it.")
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
                                              ? qsTr("Remove this Hub from the Player? Running Sessions end and you return to the connection screen. Saves on the Hub are not deleted.")
                                              : qsTr("Remove this Hub from the Player? Saves on the Hub are not deleted.")
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

                    SettingsRow {
                        objectName: "autoConnectRow"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.topMargin: 4
                        resetMode: "none"
                        label: qsTr("Connect automatically on startup")
                        description: qsTr("Uses the last active hub.")
                        ctrl: "toggle"
                        toggleName: "settingsAutoConnectToggle"
                        checked: root.player.autoConnect
                        onToggled: on => root.player.autoConnect = on
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
                }
            }
        }
    }
}
