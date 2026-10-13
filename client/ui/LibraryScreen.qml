import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// 3c: Library (reduced): shell, game grid, detail pane.
Rectangle {
    id: root
    required property PlayerController player
    color: Theme.bg

    // "Upload ROM": native file dialog (UploadFileDialog.qml, QtQuick.Dialogs) when the module is installed,
    // otherwise a path field. The dialog is its own file so that windeployqt sees the import.
    property bool pathPromptOpen: false
    Loader {
        id: fileDialogLoader
        active: false
        source: "UploadFileDialog.qml"
        onLoaded: item.player = root.player
    }
    function openUploadDialog() {
        fileDialogLoader.active = true
        if (fileDialogLoader.status === Loader.Ready && fileDialogLoader.item !== null) {
            fileDialogLoader.item.open()
        } else {
            root.pathPromptOpen = !root.pathPromptOpen
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: 232
            player: root.player
        }

        // The list area takes what the sidebar and the detail pane leave and shrinks first (wide fonts): its content
        // minimum must not push the detail pane past the window edge. The content stops at Theme.gridMaxWidth (1680 +
        // margins), left-aligned; on a 4K screen the space between it and the detail column stays empty (P7).
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            Layout.fillHeight: true

        ColumnLayout {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 28
            anchors.bottomMargin: 28
            anchors.leftMargin: 32
            width: Math.max(0, Math.min(parent.width - 64, Theme.gridMaxWidth))
            spacing: 16

            // Header (3c-6): title, search and "Upload ROM" in one row while they fit, plain anchors (no RowLayout:
            // "Upload ROM" appears when the handshake reports uploads, usually after the first layout; anchors follow
            // visibility changes immediately, P8). The title keeps its implicit width and elides only when it cannot
            // share a row with the button; search shrinks first (down to searchMin) and then wraps onto its own line.
            Item {
                id: header
                objectName: "libraryHeader"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                readonly property bool uploadShown: root.player.canUpload
                readonly property int searchMin: 140
                readonly property real buttonSpace: uploadShown ? uploadButton.implicitWidth + Theme.space12 : 0
                readonly property bool wrapped: width < libraryTitle.implicitWidth + Theme.space16 + searchMin + buttonSpace
                implicitHeight: wrapped ? 36 + Theme.space8 + 36 : 36
                FbLabel {
                    id: libraryTitle
                    objectName: "libraryTitle"
                    anchors.left: parent.left
                    y: (36 - height) / 2
                    width: Math.max(0, Math.min(implicitWidth, header.width - header.buttonSpace))
                    elide: Text.ElideRight
                    text: qsTr("Library")
                    font.pixelSize: Theme.fontPage
                    font.weight: Font.DemiBold
                    font.letterSpacing: -0.4
                }
                FbButton {
                    id: uploadButton
                    objectName: "uploadButton"
                    visible: header.uploadShown
                    anchors.right: parent.right
                    y: 0
                    enabled: !root.player.upload.active
                    implicitHeight: 36
                    text: qsTr("Upload ROM")
                    onClicked: root.openUploadDialog()
                }
                FbField {
                    id: search
                    objectName: "searchField"
                    x: header.wrapped ? 0 : header.width - header.buttonSpace - width
                    y: header.wrapped ? 36 + Theme.space8 : 0
                    // 220 at 1280, 280 on a wide window; shrinks to the room the title and button leave; full width
                    // when wrapped onto its own line.
                    width: header.wrapped ? header.width
                                          : Math.max(header.searchMin, Math.min(header.width >= 1400 ? 280 : 220,
                                                header.width - libraryTitle.implicitWidth - Theme.space16 - header.buttonSpace))
                    implicitHeight: 36
                    font.family: Qt.application.font.family
                    font.pixelSize: Theme.fontSmall
                    placeholderText: qsTr("Search…")
                    onTextChanged: root.player.library.filterText = text
                }
            }

            // Filter chips with counts ("All · 10"), sort and "Ready first" on the same row; they wrap under the chips in a
            // narrow column (3c-6). The count of the result is not repeated as text (P11).
            Item {
                id: toolbar
                objectName: "libraryToolbar"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                // Natural width of the chips in one row (the chips wrap in a Flow, so its own implicit width is not it).
                readonly property real chipsNatural: {
                    let w = 0, n = 0
                    for (let i = 0; i < chipRow.children.length; ++i) {
                        const c = chipRow.children[i]
                        if (c.visible && c.implicitWidth > 0) { w += c.implicitWidth; ++n }
                    }
                    return w + Math.max(0, n - 1) * Theme.space8
                }
                // Sort + "Ready first" side by side; in a narrow toolbar they wrap into two lines when they do not fit
                readonly property real controlsNatural: sortControl.implicitWidth + controls.spacing + readyFirst.width
                readonly property bool narrow: width < chipsNatural + controlsNatural + 24
                implicitHeight: narrow ? chipRow.height + 8 + controls.height : Math.max(32, chipRow.height)
                Flow {
                    id: chipRow
                    objectName: "filterChips"
                    width: toolbar.width
                    spacing: Theme.space8
                    Repeater {
                        model: [
                            { name: "filterAll", value: "all", label: qsTr("All") },
                            { name: "filterReady", value: "ready", label: qsTr("Ready") },
                            { name: "filterAttention", value: "attention", label: qsTr("Needs attention") },
                            { name: "filterDownload", value: "download", label: qsTr("Not downloaded") }
                        ]
                        delegate: FbChip {
                            required property var modelData
                            objectName: modelData.name
                            text: modelData.label
                            count: root.player.library.counts[modelData.value]
                            active: root.player.library.filter === modelData.value
                            onClicked: root.player.library.filter = modelData.value
                        }
                    }
                }
                Flow {
                    id: controls
                    width: toolbar.narrow ? toolbar.width : toolbar.controlsNatural
                    spacing: 16
                    y: toolbar.narrow ? chipRow.height + 8 : 0
                    x: toolbar.narrow ? 0 : toolbar.width - width
                    SortControl {
                        id: sortControl
                        current: root.player.library.sortKey
                        onPicked: (key) => root.player.library.sortKey = key
                    }
                    Item {
                        id: readyFirst
                        objectName: "readyFirstToggle"
                        readonly property bool on: root.player.library.readyFirst
                        enabled: root.player.library.readyFirstApplicable
                        opacity: enabled ? 1 : 0.45
                        width: rfRow.implicitWidth
                        height: 32
                        activeFocusOnTab: true
                        Accessible.role: Accessible.CheckBox
                        Accessible.name: qsTr("Ready first")
                        Accessible.checked: on
                        Row {
                            id: rfRow
                            spacing: 8
                            anchors.verticalCenter: parent.verticalCenter
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 32
                                height: 18
                                radius: 9
                                color: readyFirst.on ? Theme.accent : Theme.borderInput
                                border.width: readyFirst.activeFocus ? 1.5 : 0
                                border.color: Theme.accent
                                Behavior on color { ColorAnimation { duration: Theme.durFast } }
                                Rectangle {
                                    x: readyFirst.on ? 16 : 2
                                    y: 2
                                    width: 14
                                    height: 14
                                    radius: 7
                                    color: readyFirst.on ? Theme.textOnAccent : Theme.text
                                    Behavior on x { NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Ready first")
                                font.pixelSize: Theme.fontSmall
                                color: Theme.textSecondary
                            }
                        }
                        TapHandler { onTapped: { readyFirst.forceActiveFocus(); root.player.library.readyFirst = !root.player.library.readyFirst } }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        Keys.onPressed: (event) => {
                            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                root.player.library.readyFirst = !root.player.library.readyFirst
                                event.accepted = true
                            }
                        }
                    }
                }
            }
            // "3 of 10 games": only while a chip or the search narrows the list; the unfiltered count lives in "All · 10".
            FbLabel {
                id: countLabel
                objectName: "libraryCount"
                readonly property bool narrowed: root.player.library.filter !== "all" || root.player.library.filterText.trim() !== ""
                visible: narrowed
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredHeight: visible ? implicitHeight : 0
                color: Theme.textMeta
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
                text: root.player.library.countText
            }

            // Core notices (3c-6): one dismissible notice for the cores the Hub delivers at the first start (grouped by core),
            // one for cores that cannot be delivered, one per version mismatch. Part of the column, not pinned over the grid.
            Repeater {
                model: root.player.coreWarnings
                delegate: Rectangle {
                    id: warn
                    required property var modelData
                    objectName: "coreWarning"
                    readonly property bool info: modelData.tone === "info"
                    readonly property bool dismissible: modelData.code === "core_download"
                    Layout.fillWidth: true
                    implicitHeight: warnRow.implicitHeight + 20
                    radius: 8
                    color: warn.info ? Theme.infoBg : Theme.warnBg
                    border.width: 1
                    border.color: warn.info ? Theme.infoBorder : Theme.accentBorder
                    RowLayout {
                        id: warnRow
                        anchors.fill: parent
                        anchors.margins: 10
                        anchors.leftMargin: 14
                        spacing: Theme.space12
                        FbLabel {
                            id: warnText
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            wrapMode: Text.WordWrap
                            text: warn.modelData.text
                            color: warn.info ? Theme.infoText : Theme.warn
                            font.pixelSize: Theme.fontSmall
                        }
                        FbButton {
                            objectName: "coreWarningDismiss"
                            visible: warn.dismissible
                            kind: "link"
                            text: qsTr("Dismiss")
                            onClicked: root.player.dismissCoreNotice(warn.modelData.code)
                        }
                    }
                }
            }

            RowLayout {
                objectName: "uploadPathPrompt"
                Layout.fillWidth: true
                visible: root.pathPromptOpen && root.player.canUpload
                spacing: 10
                FbField {
                    id: uploadPath
                    objectName: "uploadPathField"
                    Layout.fillWidth: true
                    implicitHeight: 34
                    placeholderText: qsTr("Path of the ROM file to upload")
                    onAccepted: uploadPathButton.clicked()
                }
                FbButton {
                    id: uploadPathButton
                    objectName: "uploadPathButton"
                    implicitHeight: 34
                    text: qsTr("Upload")
                    enabled: uploadPath.text.trim() !== "" && !root.player.upload.active
                    onClicked: { root.player.uploadRom(uploadPath.text.trim()); root.pathPromptOpen = false }
                }
            }

            Rectangle {
                objectName: "uploadStatus"
                Layout.fillWidth: true
                visible: root.player.upload.active || root.player.upload.message !== ""
                implicitHeight: 34
                radius: 8
                color: root.player.upload.isError ? Theme.errorBg : Theme.surfaceRaised
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 8
                    spacing: 10
                    FbLabel {
                        Layout.fillWidth: true
                        text: root.player.upload.active
                              ? qsTr("Uploading %1 · %2 %").arg(root.player.upload.fileName).arg(Math.round(root.player.upload.progress * 100))
                              : root.player.upload.message
                        color: root.player.upload.isError ? Theme.errorText : Theme.text
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        visible: root.player.upload.active
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 4
                        radius: 2
                        color: Theme.borderInput
                        Rectangle {
                            width: parent.width * root.player.upload.progress
                            height: parent.height
                            radius: 2
                            color: Theme.accent
                        }
                    }
                    FbButton {
                        visible: !root.player.upload.active
                        kind: "link"
                        text: qsTr("Dismiss")
                        onClicked: root.player.dismissUploadMessage()
                    }
                }
            }

            // The final save write at "stop game" failed: shown until dismissed (the Hub copy is not touched).
            Rectangle {
                objectName: "stopSaveError"
                Layout.fillWidth: true
                visible: root.player.stopSaveError !== ""
                implicitHeight: stopSaveText.implicitHeight + 20
                radius: 8
                color: Theme.errorBg
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    anchors.leftMargin: 12
                    spacing: 10
                    FbLabel {
                        id: stopSaveText
                        Layout.fillWidth: true
                        text: root.player.stopSaveError
                        wrapMode: Text.WordWrap
                        color: Theme.errorText
                        font.pixelSize: Theme.fontSmall
                    }
                    FbButton { objectName: "stopSaveErrorDismiss"; kind: "link"; text: qsTr("Dismiss"); onClicked: root.player.dismissStopSaveError() }
                }
            }

            Rectangle {
                objectName: "hubLinkBanner"
                Layout.fillWidth: true
                visible: root.player.sessions.available && root.player.sessions.hubLink !== "online"
                implicitHeight: 34
                radius: 8
                color: Theme.warnBg
                FbLabel {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    verticalAlignment: Text.AlignVCenter
                    text: root.player.sessions.hubLink === "connecting" ? qsTr("Connecting to the Hub for Sessions…")
                          : qsTr("Hub connection for Sessions lost · reconnecting…")
                    color: Theme.warn
                    font.pixelSize: Theme.fontSmall
                }
            }

            Rectangle {
                objectName: "sessionMessage"
                Layout.fillWidth: true
                visible: root.player.sessions.message !== ""
                implicitHeight: 34
                radius: 8
                color: root.player.sessions.messageIsError ? Theme.errorBg : Theme.surfaceRaised
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 8
                    FbLabel {
                        Layout.fillWidth: true
                        text: root.player.sessions.message
                        color: root.player.sessions.messageIsError ? Theme.errorText : Theme.text
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                    FbButton { kind: "link"; text: qsTr("Dismiss"); onClicked: root.player.sessions.dismissMessage() }
                }
            }

            SessionsSection {
                Layout.fillWidth: true
                player: root.player
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Ready first: two grids (ready group, rest) with a divider between them; otherwise one grid with every game.
                Flickable {
                    id: gridScroll
                    objectName: "gameScroll"
                    anchors.fill: parent
                    visible: root.player.libraryState === "ready" && root.player.library.count > 0
                    clip: true
                    contentWidth: width
                    contentHeight: gridColumn.implicitHeight + 8
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { }

                    // The grid stops at Theme.gridMaxWidth (1680), left-aligned; the detail column keeps its width.
                    readonly property int gridW: Math.min(Theme.gridMaxWidth, width - 4)
                    readonly property int columns: Math.max(2, Math.floor(gridW / 170))
                    readonly property int cellW: Math.floor(gridW / columns)
                    readonly property bool dated: root.player.library.sortKey === "added_desc" || root.player.library.sortKey === "added_asc"
                    readonly property int cellH: cellW + 62 + 10 + (dated ? 16 : 0)

                    Column {
                        id: gridColumn
                        x: 4
                        y: 4
                        width: Math.min(Theme.gridMaxWidth, gridScroll.width - 4)
                        spacing: 6

                        GameGrid {
                            id: grid
                            objectName: "gameGrid"
                            width: parent.width
                            scroller: gridScroll
                            cellWidth: gridScroll.cellW
                            cellHeight: gridScroll.cellH
                            model: LibraryGroupModel { library: root.player.library; group: root.player.library.grouped ? 0 : -1 }
                            showAdded: gridScroll.dated
                            selectedId: root.player.selectedGameId || ""
                            runningId: root.player.backgroundGame.id || ""
                            onTileClicked: (gameId) => root.player.selectGame(gameId)
                        }

                        // Group divider: "NOT READY · n · same sort" (only when both groups have games).
                        RowLayout {
                            objectName: "notReadyDivider"
                            visible: root.player.library.grouped
                            width: parent.width
                            height: visible ? implicitHeight : 0
                            spacing: 12
                            Eyebrow {
                                Layout.minimumWidth: 0
                                text: qsTr("NOT READY · %1 · same sort").arg(root.player.library.notReadyGroupCount)
                                color: Theme.textFaint
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderCard }
                        }

                        GameGrid {
                            id: gridRest
                            objectName: "gameGridNotReady"
                            visible: root.player.library.grouped
                            width: parent.width
                            height: visible ? implicitHeight : 0
                            scroller: gridScroll
                            cellWidth: gridScroll.cellW
                            cellHeight: gridScroll.cellH
                            model: LibraryGroupModel { library: root.player.library; group: 1 }
                            showAdded: gridScroll.dated
                            selectedId: root.player.selectedGameId || ""
                            runningId: root.player.backgroundGame.id || ""
                            onTileClicked: (gameId) => root.player.selectGame(gameId)
                        }
                    }
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, 420)
                    spacing: 12
                    visible: !gridScroll.visible
                    FbSpinner {
                        Layout.alignment: Qt.AlignHCenter
                        visible: root.player.libraryState === "loading"
                        size: 22
                    }
                    FbLabel {
                        objectName: "libraryEmptyText"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: root.player.libraryState === "error" ? Theme.error : Theme.textMuted
                        font.pixelSize: Theme.fontBody
                        readonly property string needle: root.player.library.filterText.trim()
                        readonly property string chip: root.player.library.filter
                        text: root.player.libraryState === "loading" ? qsTr("Loading Library…")
                              : root.player.libraryState === "error" ? qsTr("Could not load Library: %1").arg(root.player.libraryError)
                              : root.player.library.totalCount === 0 ? qsTr("This hub has no games yet.")
                              : needle !== "" ? (chip === "ready" ? qsTr("No ready games match “%1”").arg(needle)
                                                 : chip === "attention" ? qsTr("No games needing attention match “%1”").arg(needle)
                                                 : chip === "download" ? qsTr("No games not downloaded match “%1”").arg(needle)
                                                 : qsTr("No games match “%1”").arg(needle))
                              : chip === "attention" ? qsTr("Nothing needs attention.")
                              : qsTr("No results.")
                    }
                    FbLabel {
                        objectName: "libraryEmptyHint"
                        visible: root.player.libraryState === "ready" && root.player.library.outsideFilter.count !== undefined
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                        text: root.player.library.outsideFilter.count === undefined ? ""
                              : root.player.library.outsideFilter.count === 1
                                ? qsTr("1 match outside this filter: %1 (%2).").arg(root.player.library.outsideFilter.title).arg(root.player.library.outsideFilter.reason)
                                : qsTr("%1 matches outside this filter, for example %2 (%3).").arg(root.player.library.outsideFilter.count)
                                    .arg(root.player.library.outsideFilter.title).arg(root.player.library.outsideFilter.reason)
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        visible: root.player.libraryState === "ready" && root.player.library.totalCount > 0
                        spacing: 10
                        FbButton {
                            objectName: "emptyClearSearch"
                            visible: root.player.library.filterText.trim() !== ""
                            text: qsTr("Clear search")
                            onClicked: search.text = ""
                        }
                        FbButton {
                            objectName: "emptyShowAll"
                            visible: root.player.library.filter !== "all"
                            text: qsTr("Show in All")
                            onClicked: root.player.library.filter = "all"
                        }
                    }
                    FbButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: root.player.libraryState === "error"
                        busyOnClick: true
                        text: qsTr("Reload")
                        onClicked: root.player.reloadLibrary()
                    }
                }
            }
        }
        }

        DetailPane {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.detailWidth
            player: root.player
        }
    }

    StartConfirmDialog { player: root.player }
}
