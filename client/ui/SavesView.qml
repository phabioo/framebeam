import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Saves mode of the Library detail column (3c-3, 3c-4): back row, slot tabs or switcher, CURRENT bar, snapshot / upload
// actions with inline confirmations, and the HISTORY timeline (SaveRow). Reuses SaveHistoryController for everything the
// Hub does; the column stays 392 wide and Play / Play and share Session stay pinned below (DetailPane).
//
// Also used inside the game (GamePanel, inGame: true): the game keeps running; snapshots, history, delete, restore and
// upload work live (restore / upload load the save into the running core and restart the game from it); slots cannot
// be changed while the game runs. "Resolve conflict" opens the inline conflict resolution in both places (it never
// starts the game).
FocusScope {
    id: root
    required property PlayerController player
    property bool inGame: false              // shown inside the game panel (live)
    property string backTitle: ""            // "← <backTitle>"; default: the game title (Library)
    readonly property SaveHistoryController hist: player.saveHistory
    readonly property var game: player.selectedGame
    readonly property string syncKind: game.syncKind || "none"
    readonly property bool conflict: syncKind === "conflict"
    // Decision ad: the switcher above three slots; also whenever the tabs would not fit the column (wide fonts), so no slot is clipped.
    readonly property bool tabsCollapsed: hist.slotCount > 3 || (tabsFitProbe.sum > 0 && tabs.width > 0 && tabsFitProbe.sum + 8 + 28 > tabs.width)
    readonly property bool hasCurrent: hist.current.revision !== undefined
    readonly property bool uploadConfirming: hist.uploadRequest.path !== undefined
    readonly property bool uploadFailed: hist.uploadFailure.path !== undefined && !hist.uploading && !uploadConfirming
    property string filter: "all"            // "all" | "snapshots"
    property bool newSlotOpen: false
    property bool snapshotOpen: false
    property bool detailsOpen: false
    property bool pathPromptOpen: false
    property string newSlotError: ""
    readonly property bool live: inGame && hist.liveApply   // restore / upload restart the running game from the save
    signal back()

    objectName: "savesView"
    readonly property bool historyAvailable: hist.available   // Hub advertises saves_v2 (history, restore, snapshots)

    // Only the saves view inside the game may load a save into the running core; the Library view never touches it.
    onInGameChanged: if (root.hist) root.hist.liveMode = root.inGame
    Component.onCompleted: if (root.inGame) hist.liveMode = true
    Component.onDestruction: if (root.inGame && root.hist) root.hist.liveMode = false

    function cancelAll() {
        hist.closeConflict()
        hist.cancelRestore()
        hist.cancelDelete()
        hist.cancelUploadFile()
        hist.dismissUploadFailure()
        root.newSlotOpen = false
        root.snapshotOpen = false
        root.detailsOpen = false
        root.pathPromptOpen = false
        root.filter = "all"
    }
    // "Upload save file...": native file dialog (SaveFileDialog.qml, QtQuick.Dialogs) when the module is installed,
    // otherwise a path field.
    function openUpload() {
        if (root.pathPromptOpen) {
            hist.requestUploadFile(uploadPath.text)
            return
        }
        fileDialogLoader.active = true
        if (fileDialogLoader.status === Loader.Ready && fileDialogLoader.item !== null) {
            fileDialogLoader.item.open()
        } else {
            root.pathPromptOpen = true
        }
    }
    function createSlot() {
        root.newSlotError = hist.createSlot(newSlotField.text)
        if (root.newSlotError === "") {
            newSlotField.text = ""
            root.newSlotOpen = false
        }
    }
    function join(parts) { return parts.filter(p => p !== undefined && p !== "").join(" · ") }

    readonly property var rows: {
        const out = []
        const cur = hist.current
        if (cur.revision !== undefined) {
            out.push({ kind: "current", version: -1, versionText: cur.revisionText, label: "", isSnapshot: false,
                       meta: join([cur.when, cur.device, cur.reasonText]) })
        }
        for (const v of hist.history) {
            if (root.filter === "snapshots" && !v.isSnapshot) continue
            out.push({ kind: v.isSnapshot ? "snap" : "plain", version: v.version, versionText: v.versionText, label: v.label,
                       isSnapshot: v.isSnapshot, meta: join([v.when, v.device, v.reasonText]) })
        }
        return out
    }

    Keys.onEscapePressed: (event) => {
        if (hist.conflictOpen) hist.closeConflict()
        else if (hist.restoreRequest.version !== undefined) hist.cancelRestore()
        else if (hist.deleteRequest.version !== undefined) hist.cancelDelete()
        else if (root.uploadConfirming) hist.cancelUploadFile()
        else if (root.newSlotOpen) root.newSlotOpen = false
        else if (root.snapshotOpen) root.snapshotOpen = false
        else root.back()
        event.accepted = true
    }

    // One side of the conflict: who saved it, when, how big.
    component ConflictSide: Rectangle {
        id: side
        property string title: ""
        property var info: ({})
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: sideCol.implicitHeight + 20
        radius: Theme.radius7
        color: Theme.bg
        ColumnLayout {
            id: sideCol
            anchors.fill: parent
            anchors.margins: 10
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 4
            Eyebrow { text: side.title }
            FbLabel {
                objectName: side.objectName + "Device"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
                text: side.info.device || qsTr("unknown device")
                font.pixelSize: Theme.fontSmall
                font.weight: Font.Medium
            }
            FbLabel {
                objectName: side.objectName + "When"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
                text: side.info.when || ""
                color: Theme.textMeta
                font.pixelSize: Theme.fontMeta
            }
            FbMono {
                objectName: side.objectName + "Size"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
                text: (side.info.revisionText ? side.info.revisionText + " · " : "") + (side.info.sizeText || "")
                color: Theme.text
                font.pixelSize: Theme.fontMeta
            }
        }
    }

    Loader {
        id: fileDialogLoader
        active: false
        source: "SaveFileDialog.qml"
        onLoaded: item.history = root.hist
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        // Back row
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: 28
            Item {
                id: backButton
                objectName: "savesBack"
                anchors.left: parent.left
                anchors.right: updated.visible ? updated.left : refresh.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                height: 28
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: backText.text
                Text {
                    id: backText
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    text: "← " + (root.backTitle !== "" ? root.backTitle : (root.game.title || qsTr("Library")))
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                    color: backHover.hovered || backButton.activeFocus ? Theme.text : Theme.textMuted
                }
                HoverHandler { id: backHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.back() }
                Keys.onPressed: (event) => { if (event.key === Qt.Key_Space || event.key === Qt.Key_Return) { root.back(); event.accepted = true } }
            }
            FbMono {
                id: updated
                objectName: "savesUpdated"
                visible: root.hist.lastUpdated !== ""
                anchors.right: refresh.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("updated %1").arg(root.hist.lastUpdated)
                color: Theme.textFaint
                font.pixelSize: Theme.fontMono
            }
            Rectangle {
                id: refresh
                objectName: "historyRefresh"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 28
                height: 28
                radius: Theme.radius6
                color: refreshHover.hovered && enabled ? Theme.surfaceRaised : "transparent"
                border.width: 1
                border.color: activeFocus ? Theme.accent : Theme.borderInput
                enabled: !root.hist.loading
                opacity: enabled ? 1 : 0.6
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Refresh from hub")
                Text {
                    id: refreshGlyph
                    anchors.centerIn: parent
                    text: "↻"
                    font.pixelSize: 15
                    color: Theme.textMuted
                    RotationAnimation on rotation { running: root.hist.loading; from: 0; to: 360; duration: 900; loops: Animation.Infinite }
                }
                HoverHandler { id: refreshHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { enabled: refresh.enabled; onTapped: root.hist.refresh() }
                Keys.onPressed: (event) => { if (refresh.enabled && (event.key === Qt.Key_Space || event.key === Qt.Key_Return)) { root.hist.refresh(); event.accepted = true } }
                ToolTip.visible: refreshHover.hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Refresh from hub")
            }
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            contentWidth: width
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }

            ColumnLayout {
                id: body
                width: flick.width
                spacing: 14

                FbLabel {
                    objectName: "savesTitle"
                    text: qsTr("Saves")
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }

                // Slot tabs, or the switcher when there are more than three slots
                Item {
                    id: tabs
                    objectName: "slotTabs"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: 30
                    readonly property bool changeable: !root.hist.gameRunning && !root.hist.busy
                    // Invisible measurement of the natural tab widths (independent of the collapsed state)
                    Item {
                        id: tabsFitProbe
                        visible: false
                        property real sum: 0
                        // "+ New slot" shrinks to a plain "+" when the full label does not fit beside the tabs.
                        readonly property bool linkCompact: sum + 8 + linkProbe.implicitWidth + 8 > tabs.width
                        Text {
                            id: linkProbe
                            visible: false
                            text: qsTr("+ New slot")
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.Medium
                        }
                        Repeater {
                            model: root.hist.slotOptions
                            onCountChanged: Qt.callLater(tabsFitProbe.recalc)
                            delegate: Text {
                                required property var modelData
                                required property int index
                                textFormat: Text.StyledText
                                text: modelData.label + (modelData.count > 0 ? " <font color='" + Theme.textFaint + "'>" + modelData.count + "</font>" : "")
                                font.pixelSize: Theme.fontSmall
                                font.weight: modelData.value === root.hist.slot ? Font.DemiBold : Font.Normal
                                onImplicitWidthChanged: tabsFitProbe.recalc()
                                Component.onCompleted: tabsFitProbe.recalc()
                            }
                        }
                        function recalc() {
                            let w = 0, n = 0
                            for (let i = 0; i < children.length; ++i) {
                                const c = children[i]
                                if (c !== linkProbe && c.implicitWidth !== undefined && c.text !== undefined) { w += Math.min(c.implicitWidth, 90); ++n }
                            }
                            sum = w + Math.max(0, n - 1) * 12
                        }
                    }
                    Row {
                        id: tabRow
                        visible: !root.tabsCollapsed
                        anchors.left: parent.left
                        anchors.right: newSlotLink.left
                        anchors.rightMargin: 8
                        height: parent.height
                        spacing: 12
                        clip: true
                        Repeater {
                            model: root.tabsCollapsed ? [] : root.hist.slotOptions
                            delegate: Item {
                                id: tab
                                required property var modelData
                                readonly property bool active: modelData.value === root.hist.slot
                                objectName: "slotTab_" + modelData.value
                                readonly property string text: tabText.text
                                height: tabRow.height
                                width: Math.min(tabText.implicitWidth, 90)
                                opacity: tabs.changeable || active ? 1 : 0.5
                                activeFocusOnTab: true
                                Accessible.role: Accessible.PageTab
                                Accessible.name: tabText.text
                                Text {
                                    id: tabText
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width
                                    elide: Text.ElideRight
                                    textFormat: Text.StyledText
                                    text: tab.modelData.label + (tab.modelData.count > 0 ? " <font color='" + Theme.textFaint + "'>" + tab.modelData.count + "</font>" : "")
                                    font.pixelSize: Theme.fontSmall
                                    font.weight: tab.active ? Font.DemiBold : Font.Normal
                                    color: tab.active ? Theme.text : Theme.textMuted
                                }
                                Rectangle {
                                    visible: tab.active
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 2
                                    color: Theme.accent
                                }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                TapHandler { enabled: tabs.changeable && !tab.active; onTapped: root.hist.selectSlot(tab.modelData.value) }
                                Keys.onPressed: (event) => {
                                    if (tabs.changeable && !tab.active && (event.key === Qt.Key_Space || event.key === Qt.Key_Return)) {
                                        root.hist.selectSlot(tab.modelData.value)
                                        event.accepted = true
                                    }
                                }
                            }
                        }
                    }
                    // Switcher "Main · 8 ▾" (more than three slots); "+ New slot" is its last item
                    Rectangle {
                        id: switcher
                        objectName: "slotSwitcher"
                        visible: root.tabsCollapsed
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        height: 30
                        width: Math.min(switcherText.implicitWidth + 36, parent.width)
                        radius: Theme.radius7
                        color: Theme.surface
                        border.width: activeFocus || switchMenu.visible ? 1.5 : 1
                        border.color: activeFocus || switchMenu.visible ? Theme.accent : Theme.borderInput
                        opacity: tabs.changeable ? 1 : 0.6
                        activeFocusOnTab: true
                        Accessible.role: Accessible.ComboBox
                        Accessible.name: qsTr("Save slot: %1").arg(switcherText.text)
                        Text {
                            id: switcherText
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, switcher.width - 36)
                            elide: Text.ElideRight
                            text: {
                                let count = 0
                                for (const o of root.hist.slotOptions) if (o.value === root.hist.slot) count = o.count
                                return root.hist.slotLabel + (count > 0 ? " · " + count : "")
                            }
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }
                        Text { anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: switchMenu.visible ? "▴" : "▾"; font.pixelSize: Theme.fontMono; color: Theme.textFaint }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler { enabled: tabs.changeable; onTapped: { switcher.forceActiveFocus(); switchMenu.visible ? switchMenu.close() : switchMenu.open() } }
                        Keys.onPressed: (event) => { if (tabs.changeable && (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Down)) { switchMenu.open(); event.accepted = true } }
                        Popup {
                            id: switchMenu
                            objectName: "slotSwitchMenu"
                            parent: switcher
                            y: switcher.height + 4
                            width: Math.max(220, switcher.width)
                            padding: 4
                            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                            background: Rectangle { radius: Theme.radius8; color: Theme.popupBg; border.width: 1; border.color: Theme.borderPopup }
                            // A plain Column (no Layout): the model can change while the popup resizes without touching stale layout items.
                            contentItem: Column {
                                spacing: 0
                                Repeater {
                                    model: root.hist.slotOptions
                                    delegate: ItemDelegate {
                                        id: slotItem
                                        required property var modelData
                                        objectName: "slotMenu_" + modelData.value
                                        width: switchMenu.availableWidth
                                        implicitHeight: 30
                                        padding: 0
                                        leftPadding: 8
                                        rightPadding: 8
                                        onClicked: { const slotValue = slotItem.modelData.value; switchMenu.close(); root.hist.selectSlot(slotValue) }
                                        contentItem: Row {
                                            spacing: 4
                                            Text { width: 16; anchors.verticalCenter: parent.verticalCenter; text: slotItem.modelData.value === root.hist.slot ? "✓" : ""; font.pixelSize: Theme.fontSmall; color: Theme.accent }
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: slotItem.width - 16 - 4 - 16
                                                elide: Text.ElideRight
                                                text: slotItem.modelData.label + (slotItem.modelData.count > 0 ? " · " + slotItem.modelData.count : "")
                                                font.pixelSize: Theme.fontSmall
                                                color: Theme.text
                                            }
                                        }
                                        background: Rectangle { radius: Theme.radius6; color: slotItem.highlighted || slotItem.hovered ? Theme.surfaceRaised : "transparent" }
                                    }
                                }
                                Item { width: switchMenu.availableWidth; height: 9; Rectangle { anchors.centerIn: parent; width: parent.width; height: 1; color: Theme.borderSidebar } }
                                ItemDelegate {
                                    id: newItem
                                    objectName: "slotMenuNew"
                                    width: switchMenu.availableWidth
                                    implicitHeight: 30
                                    padding: 0
                                    leftPadding: 24
                                    text: qsTr("+ New slot")
                                    font.pixelSize: Theme.fontSmall
                                    font.weight: Font.Medium
                                    onClicked: { switchMenu.close(); root.newSlotError = ""; root.newSlotOpen = true; Qt.callLater(() => newSlotField.forceActiveFocus()) }
                                    background: Rectangle { radius: Theme.radius6; color: newItem.highlighted || newItem.hovered ? Theme.surfaceRaised : "transparent" }
                                }
                            }
                        }
                    }
                    FbButton {
                        id: newSlotLink
                        objectName: "newSlotButton"
                        visible: !root.tabsCollapsed
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        kind: "link"
                        implicitHeight: 28
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.Medium
                        text: tabsFitProbe.linkCompact ? "+" : qsTr("+ New slot")
                        Accessible.name: qsTr("New slot")
                        enabled: !root.hist.gameRunning
                        onClicked: {
                            root.newSlotError = ""
                            root.newSlotOpen = !root.newSlotOpen
                            if (root.newSlotOpen) newSlotField.forceActiveFocus()
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSidebar; z: -1 }
                }

                // New slot (inline)
                ColumnLayout {
                    objectName: "newSlotRow"
                    visible: root.newSlotOpen
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 8
                    FbField {
                        id: newSlotField
                        objectName: "newSlotField"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        implicitHeight: 34
                        placeholderText: qsTr("slot-name")
                        maximumLength: 64
                        onTextChanged: root.newSlotError = ""
                        onAccepted: root.createSlot()
                    }
                    FbLabel {
                        objectName: "newSlotError"
                        visible: root.newSlotError !== ""
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: root.newSlotError
                        color: Theme.error
                        font.pixelSize: Theme.fontMeta
                    }
                    FbLabel {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("A new slot starts without a save. “%1” is not changed.").arg(root.hist.slotLabel)
                        color: Theme.textMeta
                        font.pixelSize: Theme.fontMeta
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 8
                        Item { Layout.fillWidth: true }
                        FbButton { objectName: "newSlotCancel"; implicitHeight: 32; font.pixelSize: Theme.fontSmall; text: qsTr("Cancel"); onClicked: root.newSlotOpen = false }
                        FbButton { objectName: "newSlotCreate"; kind: "primary"; implicitHeight: 32; font.pixelSize: Theme.fontSmall; text: qsTr("Create slot"); onClicked: root.createSlot() }
                    }
                }

                // Banners: Hub offline, game running, save conflict, pending sync
                Rectangle {
                    objectName: "savesOfflineBanner"
                    visible: !root.hist.online
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: offlineCol.implicitHeight + 20
                    radius: Theme.radius8
                    color: Theme.warnBg
                    ColumnLayout {
                        id: offlineCol
                        anchors.fill: parent
                        anchors.margins: 10
                        anchors.leftMargin: 12
                        spacing: 6
                        FbLabel {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: root.hist.lastSynced !== ""
                                  ? qsTr("Hub offline · read-only. Showing the history from the last sync, %1. Restore, snapshots and uploads need the hub.").arg(root.hist.lastSynced)
                                  : qsTr("Hub offline · read-only. Restore, snapshots and uploads need the hub.")
                            color: Theme.warn
                            font.pixelSize: Theme.fontMeta
                        }
                        FbButton { objectName: "savesTryAgain"; kind: "link"; implicitHeight: 24; font.pixelSize: Theme.fontMeta; text: qsTr("Try again"); onClicked: root.hist.refresh() }
                    }
                }
                Rectangle {
                    objectName: "savesRunningBanner"
                    visible: root.hist.online && root.hist.gameRunning
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: runningText.implicitHeight + 20
                    radius: Theme.radius8
                    color: Theme.accentChipBg
                    FbLabel {
                        id: runningText
                        anchors.fill: parent
                        anchors.margins: 10
                        anchors.leftMargin: 12
                        wrapMode: Text.WordWrap
                        text: root.live ? qsTr("%1 keeps running. Snapshots and delete work live. Restore and upload restart it from that save. Slots cannot change while it runs.").arg(root.game.title || "")
                                        : root.inGame ? qsTr("%1 keeps running. Snapshots still work. Restore, upload and slot changes are possible after the game is closed.").arg(root.game.title || "")
                                                      : qsTr("%1 is running on this device. Restore and upload are possible after the game is closed. Snapshots still work.").arg(root.game.title || "")
                        color: Theme.accent
                        font.pixelSize: Theme.fontMeta
                    }
                }
                Rectangle {
                    objectName: "savesConflictBanner"
                    visible: root.hist.online && root.conflict && !root.hist.conflictOpen
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: conflictCol.implicitHeight + 24
                    radius: Theme.radius9
                    color: Theme.accentChipBg
                    border.width: 1
                    border.color: Theme.confirmBorder
                    ColumnLayout {
                        id: conflictCol
                        anchors.fill: parent
                        anchors.margins: 12
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 8
                        Eyebrow { Layout.fillWidth: true; text: qsTr("▲ Save conflict · %1").arg(root.hist.slotLabel); color: Theme.warn }
                        FbLabel {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Restore, upload and new slot are paused for “%1” until the conflict is resolved. Other slots work normally.").arg(root.hist.slotLabel)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontMeta
                        }
                        FbButton {
                            objectName: "resolveConflictButton"
                            implicitHeight: 32
                            font.pixelSize: Theme.fontSmall
                            text: qsTr("Resolve conflict")
                            enabled: !root.hist.confirmationOpen && !root.hist.busy
                            onClicked: root.hist.openConflict()
                        }
                        FbLabel {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Compare both versions here and choose one. The game does not start.")
                            color: Theme.textFaint
                            font.pixelSize: Theme.fontMeta
                        }
                    }
                }
                // Inline conflict resolution: this device vs the Hub
                Rectangle {
                    objectName: "conflictBox"
                    visible: root.hist.conflictOpen
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: resolveCol.implicitHeight + 28
                    radius: Theme.radius9
                    color: Theme.accentChipBg
                    border.width: 1
                    border.color: Theme.confirmBorder
                    ColumnLayout {
                        id: resolveCol
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10
                        Eyebrow { Layout.fillWidth: true; text: qsTr("▲ Resolve conflict · %1").arg(root.hist.slotLabel); color: Theme.warn }
                        RowLayout {
                            objectName: "conflictLoading"
                            visible: root.hist.conflictLoading
                            Layout.fillWidth: true
                            spacing: 8
                            FbSpinner { size: 14 }
                            FbLabel {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                elide: Text.ElideRight
                                text: qsTr("Loading both versions…")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontMeta
                            }
                        }
                        FbLabel {
                            visible: !root.hist.conflictLoading
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("This device and the Hub both changed “%1”. Choose which save becomes the current one. Nothing is deleted.").arg(root.hist.slotLabel)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontMeta
                        }
                        ConflictSide {
                            objectName: "conflictLocal"
                            visible: !root.hist.conflictLoading
                            title: qsTr("This device")
                            info: root.hist.conflictInfo.local || ({})
                        }
                        ConflictSide {
                            objectName: "conflictHub"
                            visible: !root.hist.conflictLoading
                            title: qsTr("Hub")
                            info: root.hist.conflictInfo.hub || ({})
                        }
                        FbLabel {
                            visible: !root.hist.conflictLoading
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: root.inGame
                                  ? qsTr("“Use the Hub save” saves this device's copy as a local backup first and restarts the game from the Hub save. “Keep this device's save” uploads it as the new current version; the game keeps running.")
                                  : qsTr("“Use the Hub save” saves this device's copy as a local backup first. “Keep this device's save” uploads it as the new current version. The game does not start.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontMeta
                        }
                        Flow {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 8
                            layoutDirection: Qt.RightToLeft
                            FbButton {
                                objectName: "conflictKeepLocal"
                                kind: "primary"
                                implicitHeight: 32
                                font.pixelSize: Theme.fontSmall
                                text: qsTr("Keep this device's save")
                                enabled: !root.hist.conflictLoading && !root.hist.conflictBusy && (root.hist.conflictInfo.local || ({})).exists === true
                                onClicked: root.hist.resolveConflict("use_local")
                            }
                            FbButton {
                                objectName: "conflictUseHub"
                                implicitHeight: 32
                                font.pixelSize: Theme.fontSmall
                                text: qsTr("Use the Hub save")
                                enabled: !root.hist.conflictLoading && !root.hist.conflictBusy
                                onClicked: root.hist.resolveConflict("use_hub")
                            }
                            FbButton {
                                objectName: "conflictCancel"
                                implicitHeight: 32
                                font.pixelSize: Theme.fontSmall
                                text: qsTr("Cancel")
                                enabled: !root.hist.conflictBusy
                                onClicked: root.hist.closeConflict()
                            }
                        }
                    }
                }
                FbLabel {
                    objectName: "restoreBlockReason"
                    visible: root.hist.online && !root.hist.gameRunning && !root.conflict && root.hist.restoreBlockReason !== ""
                             && (root.hist.history.length > 0 || root.hist.canUploadFile)
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Restore and upload are off: %1").arg(root.hist.restoreBlockReason)
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontMeta
                }

                // CURRENT bar
                Rectangle {
                    objectName: "currentBar"
                    visible: root.hasCurrent
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: currentCol.implicitHeight + 20
                    radius: Theme.radius8
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.borderCard
                    ColumnLayout {
                        id: currentCol
                        anchors.fill: parent
                        anchors.topMargin: 10
                        anchors.bottomMargin: 10
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 8
                            Eyebrow { text: qsTr("Current") }
                            FbMono {
                                objectName: "currentVersion"
                                text: root.hist.current.revisionText || ""
                                color: Theme.text
                                font.pixelSize: Theme.fontSmall
                                font.weight: Font.Medium
                            }
                            Item { Layout.fillWidth: true }
                            FbPill {
                                objectName: "currentPill"
                                visible: text !== ""
                                small: true
                                tone: !root.hist.online ? "neutral" : (root.syncKind === "pending" ? "warn" : "ok")
                                text: !root.hist.online ? qsTr("Offline")
                                      : root.syncKind === "pending" ? qsTr("⟳ Sync pending")
                                      : root.syncKind === "synced" ? qsTr("✓ Synced") : ""
                            }
                        }
                        FbLabel {
                            objectName: "currentMeta"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: root.join([root.hist.current.when, root.hist.current.device, root.hist.current.reasonText])
                            color: Theme.textMeta
                            font.pixelSize: Theme.fontMeta
                        }
                        Item {
                            id: detailsToggle
                            objectName: "fileDetailsToggle"
                            Layout.preferredHeight: 22
                            Layout.preferredWidth: detailsToggleText.implicitWidth
                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: detailsToggleText.text
                            Text {
                                id: detailsToggleText
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.detailsOpen ? qsTr("File details ▾") : qsTr("File details ▸")
                                font.pixelSize: Theme.fontMeta
                                color: Theme.textMuted
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: root.detailsOpen = !root.detailsOpen }
                            Keys.onPressed: (event) => { if (event.key === Qt.Key_Space || event.key === Qt.Key_Return) { root.detailsOpen = !root.detailsOpen; event.accepted = true } }
                        }
                        GridLayout {
                            objectName: "fileDetails"
                            visible: root.detailsOpen
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 6
                            FbLabel { text: qsTr("SHA-256"); color: Theme.textMeta; font.pixelSize: Theme.fontMeta; Layout.alignment: Qt.AlignTop }
                            FbMono { objectName: "detailSha"; Layout.fillWidth: true; text: root.hist.current.shaShort || ""; color: Theme.text; font.pixelSize: Theme.fontMeta; wrapMode: Text.WrapAnywhere }
                            FbLabel { text: qsTr("Size"); color: Theme.textMeta; font.pixelSize: Theme.fontMeta; Layout.alignment: Qt.AlignTop }
                            FbMono { objectName: "detailSize"; Layout.fillWidth: true; text: root.hist.current.sizeText || ""; color: Theme.text; font.pixelSize: Theme.fontMeta }
                            FbLabel { text: qsTr("Local path"); color: Theme.textMeta; font.pixelSize: Theme.fontMeta; Layout.alignment: Qt.AlignTop }
                            FbMono { objectName: "detailPath"; Layout.fillWidth: true; text: root.hist.current.localPath || ""; color: Theme.text; font.pixelSize: Theme.fontMeta; wrapMode: Text.WrapAnywhere }
                            Item { Layout.preferredWidth: 1; Layout.preferredHeight: 1 }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 8
                                FbButton { objectName: "copyHashButton"; implicitHeight: 28; font.pixelSize: Theme.fontMeta; text: qsTr("Copy hash"); onClicked: root.hist.copyText(root.hist.current.sha256 || "") }
                                FbButton { objectName: "openFolderButton"; implicitHeight: 28; font.pixelSize: Theme.fontMeta; text: qsTr("Open folder"); onClicked: root.hist.openSaveFolder() }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                }

                // Message of the last action (snapshot created, restored, deleted, errors)
                Rectangle {
                    objectName: "historyMessage"
                    visible: root.hist.message !== "" && !root.uploadFailed
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: msgRow.implicitHeight + 16
                    radius: Theme.radius8
                    color: root.hist.messageIsError ? Theme.errorBg : Theme.okBg
                    RowLayout {
                        id: msgRow
                        anchors.fill: parent
                        anchors.margins: 8
                        FbLabel {
                            objectName: "historyMessageText"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            wrapMode: Text.WordWrap
                            text: (root.hist.messageIsError ? "" : "✓ ") + root.hist.message
                            font.pixelSize: Theme.fontMeta
                            color: root.hist.messageIsError ? Theme.errorText : Theme.ok
                        }
                        FbButton { kind: "link"; text: qsTr("Dismiss"); onClicked: root.hist.dismissMessage() }
                    }
                }

                // Actions
                Flow {
                    objectName: "saveActions"
                    visible: root.historyAvailable && !root.snapshotOpen && !root.uploadConfirming && !root.hist.uploading && !root.uploadFailed
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 8
                    FbButton {
                        objectName: "snapshotButton"
                        implicitHeight: 32
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("◆ Create snapshot")
                        enabled: root.hist.canSnapshot && root.hist.online
                        onClicked: { root.snapshotOpen = true; Qt.callLater(() => snapshotLabel.forceActiveFocus()) }
                    }
                    FbButton {
                        objectName: "uploadSaveButton"
                        visible: root.hist.canUploadFile || !root.hist.online
                        kind: "link"
                        implicitHeight: 32
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.Medium
                        text: root.pathPromptOpen ? qsTr("Choose") : qsTr("Upload save file…")
                        enabled: root.hist.restoreBlockReason === "" && !root.hist.busy && root.hist.online && root.hist.canUploadFile
                        onClicked: root.openUpload()
                    }
                }
                FbField {
                    id: uploadPath
                    objectName: "uploadPathField"
                    visible: root.pathPromptOpen && !root.uploadConfirming
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    implicitHeight: 34
                    placeholderText: qsTr("Path to the save file")
                    onAccepted: root.hist.requestUploadFile(text)
                }

                // Create snapshot (inline)
                Rectangle {
                    objectName: "snapshotForm"
                    visible: root.snapshotOpen
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: snapCol.implicitHeight + 28
                    radius: Theme.radius9
                    color: Theme.accentChipBg
                    border.width: 1
                    border.color: Theme.confirmBorder
                    ColumnLayout {
                        id: snapCol
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10
                        FbLabel {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: root.hist.current.revisionText ? qsTr("◆ Create snapshot of %1").arg(root.hist.current.revisionText) : qsTr("◆ Create snapshot")
                            color: Theme.accent
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                        }
                        FbField {
                            id: snapshotLabel
                            objectName: "snapshotLabel"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            implicitHeight: 34
                            placeholderText: qsTr("Label (optional)")
                            maximumLength: 64
                            onAccepted: snapshotCreate.clicked()
                        }
                        FbLabel {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Label is optional. Snapshots are kept until you delete them.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontMeta
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 8
                            Item { Layout.fillWidth: true }
                            FbButton { objectName: "snapshotCancel"; implicitHeight: 30; font.pixelSize: Theme.fontSmall; text: qsTr("Cancel"); onClicked: { snapshotLabel.text = ""; root.snapshotOpen = false } }
                            FbButton {
                                id: snapshotCreate
                                objectName: "snapshotCreate"
                                kind: "primary"
                                implicitHeight: 30
                                font.pixelSize: Theme.fontSmall
                                text: qsTr("Create snapshot")
                                enabled: root.hist.canSnapshot
                                onClicked: {
                                    root.hist.createSnapshot(snapshotLabel.text)
                                    snapshotLabel.text = ""
                                    root.snapshotOpen = false
                                }
                            }
                        }
                    }
                }

                // Upload confirmation (3c-4)
                Rectangle {
                    objectName: "uploadConfirmBox"
                    visible: root.uploadConfirming
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: uploadCol.implicitHeight + 28
                    radius: Theme.radius9
                    color: Theme.accentChipBg
                    border.width: 1
                    border.color: Theme.confirmBorder
                    ColumnLayout {
                        id: uploadCol
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 12
                        FbLabel {
                            objectName: "uploadConfirmTitle"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Upload as the new current version?")
                            color: Theme.accent
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            implicitHeight: dataGrid.implicitHeight + 20
                            radius: Theme.radius7
                            color: Theme.bg
                            GridLayout {
                                id: dataGrid
                                anchors.fill: parent
                                anchors.margins: 10
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                columns: 2
                                columnSpacing: 14
                                rowSpacing: 6
                                FbLabel { text: qsTr("File"); color: Theme.textMeta; font.pixelSize: Theme.fontSmall }
                                FbMono { objectName: "uploadFileName"; Layout.fillWidth: true; text: root.hist.uploadRequest.fileName || ""; color: Theme.text; font.pixelSize: Theme.fontSmall; elide: Text.ElideRight }
                                FbLabel { text: qsTr("Size"); color: Theme.textMeta; font.pixelSize: Theme.fontSmall }
                                FbMono { objectName: "uploadSize"; Layout.fillWidth: true; text: root.hist.uploadRequest.sizeText || ""; color: Theme.text; font.pixelSize: Theme.fontSmall; elide: Text.ElideRight }
                                FbLabel { text: qsTr("Into slot"); color: Theme.textMeta; font.pixelSize: Theme.fontSmall }
                                FbLabel {
                                    objectName: "uploadSlot"
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: root.hist.uploadRequest.currentText ? qsTr("%1 · becomes %2").arg(root.hist.slotLabel).arg(root.hist.uploadRequest.nextText || "")
                                                                               : qsTr("%1 · first save").arg(root.hist.slotLabel)
                                    font.pixelSize: Theme.fontSmall
                                }
                            }
                        }
                        FbLabel {
                            objectName: "uploadConfirmNote"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: (root.hist.uploadRequest.currentText
                                   ? qsTr("The current version is kept in the history as “Before upload”. A local backup is made first.")
                                   : qsTr("A local backup is made first."))
                                  + (root.live ? "\n" + qsTr("The game restarts from this save.") : "")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontMeta
                        }
                        Flow {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 8
                            layoutDirection: Qt.RightToLeft
                            FbButton { objectName: "uploadConfirm"; kind: "primary"; implicitHeight: 32; font.pixelSize: Theme.fontSmall; text: qsTr("Upload as new current"); onClicked: root.hist.confirmUploadFile() }
                            FbButton { objectName: "uploadCancel"; implicitHeight: 32; font.pixelSize: Theme.fontSmall; text: qsTr("Cancel"); onClicked: root.hist.cancelUploadFile() }
                        }
                    }
                }

                // Upload in progress
                Rectangle {
                    objectName: "uploadProgressBox"
                    visible: root.hist.uploading
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: progCol.implicitHeight + 28
                    radius: Theme.radius9
                    color: Theme.accentChipBg
                    border.width: 1
                    border.color: Theme.confirmBorder
                    ColumnLayout {
                        id: progCol
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 8
                            FbSpinner { size: 14 }
                            FbLabel {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: qsTr("Uploading to “%1”").arg(root.hist.slotLabel)
                                color: Theme.accent
                                font.pixelSize: Theme.fontBody
                                font.weight: Font.DemiBold
                            }
                        }
                        FbLabel {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Play is disabled until the upload finishes.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontMeta
                        }
                    }
                }

                // Upload failed
                Rectangle {
                    objectName: "uploadFailedBox"
                    visible: root.uploadFailed
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: failCol.implicitHeight + 28
                    radius: Theme.radius9
                    color: Theme.errorBg
                    border.width: 1
                    border.color: Theme.dangerBorder
                    ColumnLayout {
                        id: failCol
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8
                        FbLabel { Layout.fillWidth: true; text: qsTr("✕ Upload failed"); color: Theme.error; font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold }
                        FbLabel {
                            objectName: "uploadFailedText"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: root.hist.message
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontMeta
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 8
                            Item { Layout.fillWidth: true }
                            FbButton { objectName: "uploadFailedCancel"; implicitHeight: 30; font.pixelSize: Theme.fontSmall; text: qsTr("Cancel"); onClicked: { root.hist.dismissUploadFailure(); root.hist.dismissMessage() } }
                            FbButton { objectName: "uploadRetry"; kind: "primary"; implicitHeight: 30; font.pixelSize: Theme.fontSmall; text: qsTr("Try again"); enabled: root.hist.online; onClicked: root.hist.retryUpload() }
                        }
                    }
                }

                FbLabel {
                    objectName: "historyUnsupported"
                    visible: !root.historyAvailable
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("This Hub keeps no version history. Update the Hub to restore versions or create snapshots.")
                    color: Theme.textMeta
                    font.pixelSize: Theme.fontMeta
                }

                // HISTORY
                RowLayout {
                    objectName: "historyHeader"
                    visible: root.historyAvailable
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.topMargin: 2
                    spacing: 8
                    Eyebrow { text: qsTr("History") }
                    Item { Layout.fillWidth: true }
                    FbSegment {
                        objectName: "historyFilter"
                        Layout.maximumWidth: 200
                        Layout.minimumWidth: 0
                        segmentHeight: 22
                        current: root.filter
                        options: [
                            { value: "all", label: qsTr("All"), name: "historyFilterAll" },
                            { value: "snapshots", label: qsTr("Snapshots"), name: "historyFilterSnapshots" }
                        ]
                        onPicked: (v) => root.filter = v
                    }
                }

                FbLabel {
                    objectName: "historyLoading"
                    visible: root.historyAvailable && root.hist.loading && !root.hasCurrent && root.hist.history.length === 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Loading history from %1…").arg(root.player.hubAddress)
                    color: Theme.textMeta
                    font.pixelSize: Theme.fontMeta
                }
                ColumnLayout {
                    objectName: "historyEmpty"
                    visible: root.historyAvailable && !root.hist.loading && !root.hasCurrent && root.hist.history.length === 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 4
                    FbLabel { text: qsTr("No saves yet"); font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold }
                    FbLabel {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("The first save appears in slot “%1” after you play.").arg(root.hist.slotLabel)
                        color: Theme.textMeta
                        font.pixelSize: Theme.fontMeta
                    }
                }
                FbLabel {
                    objectName: "snapshotsEmpty"
                    visible: root.historyAvailable && root.filter === "snapshots" && root.rows.length <= (root.hasCurrent ? 1 : 0) && (root.hasCurrent || root.hist.history.length > 0)
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("No snapshots yet. A snapshot keeps a version until you delete it.")
                    color: Theme.textMeta
                    font.pixelSize: Theme.fontMeta
                }

                // Timeline
                ColumnLayout {
                    objectName: "timeline"
                    visible: root.historyAvailable
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 0
                    Repeater {
                        model: root.rows
                        delegate: SaveRow {
                            id: row
                            required property var modelData
                            required property int index
                            objectName: modelData.kind === "current" ? "historyRowCurrent" : "historyRow"
                            kind: modelData.kind
                            versionText: modelData.versionText
                            label: modelData.label
                            meta: modelData.meta
                            last: index === root.rows.length - 1
                            showRestore: modelData.kind !== "current"
                            showDelete: modelData.isSnapshot === true
                            restoreEnabled: root.hist.restoreBlockReason === "" && !root.hist.busy && root.hist.online
                            deleteEnabled: root.hist.canDeleteSnapshot
                            muted: !root.hist.online
                            confirm: modelData.version === root.hist.restoreRequest.version ? "restore"
                                     : modelData.version === root.hist.deleteRequest.version ? "delete" : ""
                            confirmTitle: confirm === "delete"
                                          ? (modelData.label !== "" ? qsTr("Delete snapshot %1 “%2”?").arg(modelData.versionText).arg(modelData.label)
                                                                    : qsTr("Delete snapshot %1?").arg(modelData.versionText))
                                          : qsTr("Restore %1 as the current version of “%2”?").arg(modelData.versionText).arg(root.hist.slotLabel)
                            confirmBody: confirm === "delete"
                                         ? qsTr("Only this snapshot is removed. The current version and all other versions stay.")
                                         : qsTr("The current version %1 stays in the history as “Before restore”.").arg(root.hist.current.revisionText || "")
                           + (root.live ? "\n" + qsTr("The game restarts from this save. A local backup is made first.") : "")
                            confirmOk: confirm === "delete" ? qsTr("Delete snapshot") : qsTr("Restore %1").arg(modelData.versionText)
                            onRestoreClicked: root.hist.requestRestore(modelData.version)
                            onDeleteClicked: root.hist.requestDelete(modelData.version)
                            onConfirmed: confirm === "delete" ? root.hist.confirmDelete() : root.hist.confirmRestore()
                            onCancelled: confirm === "delete" ? root.hist.cancelDelete() : root.hist.cancelRestore()
                        }
                    }
                }
            }
        }
    }
}
