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

        ColumnLayout {
            // The list area takes what the sidebar and the detail pane leave and shrinks first (wide fonts): its
            // content minimum must not push the detail pane past the window edge.
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            Layout.margins: 28
            Layout.leftMargin: 32
            Layout.rightMargin: 32
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0   // the header's content minimum must never widen the whole column
                spacing: 12
                FbLabel { text: qsTr("Library"); font.pixelSize: 26; font.weight: Font.DemiBold; font.letterSpacing: -0.4 }
                Item { Layout.fillWidth: true }
                FbButton {
                    objectName: "uploadButton"
                    visible: root.player.canUpload
                    enabled: !root.player.upload.active
                    implicitHeight: 36
                    text: qsTr("Upload ROM")
                    onClicked: root.openUploadDialog()
                }
                FbField {
                    id: search
                    objectName: "searchField"
                    implicitWidth: 240
                    Layout.fillWidth: true
                    Layout.maximumWidth: 240
                    Layout.minimumWidth: 0
                    implicitHeight: 36
                    font.family: Qt.application.font.family
                    font.pixelSize: Theme.fontSmall
                    placeholderText: qsTr("Search…")
                    onTextChanged: root.player.library.filterText = text
                }
            }

            // Game paused in the background (interim UI): resume or quit it.
            Rectangle {
                objectName: "runningStrip"
                Layout.fillWidth: true
                visible: root.player.backgroundGame.id !== undefined
                implicitHeight: 48
                radius: Theme.radius8
                color: Theme.okBg
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 8
                    spacing: 10
                    FbLabel {
                        objectName: "runningStripText"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: qsTr("Now running: %1 · paused").arg(root.player.backgroundGame.title || "")
                        color: Theme.ok
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    FbButton {
                        objectName: "stripResumeButton"
                        kind: "primary"
                        implicitHeight: 34
                        text: qsTr("Resume")
                        onClicked: root.player.resumeGame()
                    }
                    FbButton {
                        objectName: "stripQuitButton"
                        implicitHeight: 34
                        text: qsTr("Quit game")
                        onClicked: root.player.quitGame()
                    }
                }
            }

            // Filter chips with counts (3c, D14); combined with the search field.
            RowLayout {
                objectName: "filterChips"
                Layout.fillWidth: true
                Layout.minimumWidth: 0   // a narrow column must not be widened by the chips (it would widen every sibling, e.g. the running strip)
                spacing: Theme.space8
                Repeater {
                    model: [
                        { name: "filterAll", value: "all", label: qsTr("All"), badge: false },
                        { name: "filterReady", value: "ready", label: qsTr("Ready"), badge: false },
                        { name: "filterAttention", value: "attention", label: qsTr("Needs attention"), badge: true },
                        { name: "filterDownload", value: "download", label: qsTr("Not downloaded"), badge: false }
                    ]
                    delegate: FbChip {
                        required property var modelData
                        objectName: modelData.name
                        text: modelData.label
                        count: root.player.library.counts[modelData.value]
                        badgeAccent: modelData.badge
                        active: root.player.library.filter === modelData.value
                        onClicked: root.player.library.filter = modelData.value
                    }
                }
                Item { Layout.fillWidth: true }
            }

            // Toolbar (3c-2): count of the current result, sort select, "Ready first". Wraps to two lines in a narrow column.
            Item {
                id: toolbar
                objectName: "libraryToolbar"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                readonly property bool narrow: width < countLabel.implicitWidth + controls.implicitWidth + 24
                implicitHeight: (narrow ? 32 + 8 + 32 : 32) + 12
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSidebar }
                FbLabel {
                    id: countLabel
                    objectName: "libraryCount"
                    anchors.left: parent.left
                    anchors.right: toolbar.narrow ? parent.right : controls.left
                    anchors.rightMargin: 12
                    height: 32
                    verticalAlignment: Text.AlignVCenter
                    color: Theme.textMeta
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                    text: root.player.library.countText
                }
                Row {
                    id: controls
                    spacing: 16
                    y: toolbar.narrow ? 40 : 0
                    x: toolbar.narrow ? 0 : toolbar.width - width
                    SortControl {
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

            // Core warnings from the handshake (non-blocking)
            Repeater {
                model: root.player.coreWarnings
                delegate: Rectangle {
                    id: warn
                    required property var modelData
                    objectName: "coreWarning"
                    Layout.fillWidth: true
                    implicitHeight: warnText.implicitHeight + 20
                    radius: 8
                    color: Theme.warnBg
                    FbLabel {
                        id: warnText
                        anchors.fill: parent
                        anchors.margins: 10
                        anchors.leftMargin: 12
                        wrapMode: Text.WordWrap
                        verticalAlignment: Text.AlignVCenter
                        text: warn.modelData.text
                        color: Theme.warn
                        font.pixelSize: Theme.fontSmall
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

                    readonly property int columns: Math.max(2, Math.floor((width - 4) / 170))
                    readonly property int cellW: Math.floor((width - 4) / columns)
                    readonly property bool dated: root.player.library.sortKey === "added_desc" || root.player.library.sortKey === "added_asc"
                    readonly property int cellH: cellW + 62 + 10 + (dated ? 16 : 0)

                    Column {
                        id: gridColumn
                        x: 4
                        y: 4
                        width: gridScroll.width - 4
                        spacing: 6

                        GridView {
                            id: grid
                            objectName: "gameGrid"
                            width: parent.width
                            height: contentHeight
                            interactive: false
                            cellWidth: gridScroll.cellW
                            cellHeight: gridScroll.cellH
                            model: LibraryGroupModel { library: root.player.library; group: root.player.library.grouped ? 0 : -1 }
                            add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durList } }
                            displaced: Transition { NumberAnimation { properties: "x,y"; duration: Theme.durList; easing.type: Easing.OutCubic } }
                            delegate: GameTile {
                                width: grid.cellWidth
                                height: grid.cellHeight
                                showAdded: gridScroll.dated
                                selected: gameId === root.player.selectedGameId
                                running: root.player.backgroundGame.id === gameId
                                onClicked: root.player.selectGame(gameId)
                            }
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

                        GridView {
                            id: gridRest
                            objectName: "gameGridNotReady"
                            visible: root.player.library.grouped
                            width: parent.width
                            height: visible ? contentHeight : 0
                            interactive: false
                            cellWidth: gridScroll.cellW
                            cellHeight: gridScroll.cellH
                            model: LibraryGroupModel { library: root.player.library; group: 1 }
                            displaced: Transition { NumberAnimation { properties: "x,y"; duration: Theme.durList; easing.type: Easing.OutCubic } }
                            delegate: GameTile {
                                width: gridRest.cellWidth
                                height: gridRest.cellHeight
                                showAdded: gridScroll.dated
                                selected: gameId === root.player.selectedGameId
                                running: root.player.backgroundGame.id === gameId
                                onClicked: root.player.selectGame(gameId)
                            }
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

        DetailPane {
            Layout.fillHeight: true
            Layout.preferredWidth: 392
            player: root.player
        }
    }

    StartConfirmDialog { player: root.player }
}
