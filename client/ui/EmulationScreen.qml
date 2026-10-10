import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// 3e: Emulation. Second column (Defaults, systems with "N changed") | main (category chips, options).
// Settings are local to this device; only explicitly changed values are stored. Options of a system are exactly
// those the core reports (no invented options).
Rectangle {
    id: root
    required property PlayerController player
    readonly property var emu: root.player.emulation
    color: Theme.bg

    function toneColor(tone: string): color {
        return tone === "neutral" ? Theme.textMuted : Theme.toneColor(tone)
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
            title: qsTr("Emulation")

            ColumnItem {
                objectName: "defaultsCard"
                selected: root.emu.defaultsSelected
                onClicked: root.emu.level = "global"
                RowLayout {
                    Layout.fillWidth: true
                    FbLabel { Layout.fillWidth: true; text: qsTr("Defaults"); font.weight: Font.DemiBold }
                    FbLabel {
                        objectName: "defaultsChanged"
                        visible: root.emu.defaultsChangedCount > 0
                        text: qsTr("%1 changed").arg(root.emu.defaultsChangedCount)
                        font.pixelSize: Theme.fontMono
                        color: Theme.accent
                    }
                }
                FbLabel { text: qsTr("Apply to every system"); color: Theme.textMuted; font.pixelSize: Theme.fontMeta }
            }

            Eyebrow { text: qsTr("Systems"); Layout.leftMargin: 4; Layout.topMargin: Theme.space14; Layout.bottomMargin: Theme.space6 }

            Repeater {
                model: root.emu.systems
                delegate: ColumnItem {
                    id: card
                    required property var modelData
                    objectName: "systemCard_" + modelData.id
                    selected: !root.emu.defaultsSelected && !root.emu.gameSelected && modelData.id === root.emu.selectedSystem
                    onClicked: { root.emu.level = "system"; root.emu.selectSystem(card.modelData.id) }
                    RowLayout {
                        Layout.fillWidth: true
                        FbLabel { Layout.fillWidth: true; text: card.modelData.name; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        FbLabel {
                            objectName: "systemChanged_" + card.modelData.id
                            visible: card.modelData.changedCount > 0
                            text: qsTr("%1 changed").arg(card.modelData.changedCount)
                            font.pixelSize: Theme.fontMono
                            color: Theme.accent
                        }
                    }
                    FbLabel {
                        objectName: "systemCore_" + card.modelData.id
                        Layout.fillWidth: true
                        text: card.modelData.coreVersion !== ""
                              ? qsTr("%1 · %2").arg(card.modelData.coreName).arg(card.modelData.coreVersion)
                              : card.modelData.coreName
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontMeta
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        spacing: Theme.space6
                        StatusDot { tone: card.modelData.readyTone }
                        FbLabel {
                            objectName: "systemReady_" + card.modelData.id
                            Layout.fillWidth: true
                            text: card.modelData.readyText
                            color: root.toneColor(card.modelData.readyTone)
                            font.pixelSize: Theme.fontMeta
                            elide: Text.ElideRight
                        }
                    }
                    FbLabel {
                        objectName: "systemFirmware_" + card.modelData.id
                        Layout.fillWidth: true
                        text: card.modelData.firmwareText
                        color: root.toneColor(card.modelData.firmwareTone)
                        font.pixelSize: Theme.fontMeta
                        elide: Text.ElideRight
                    }
                }
            }

            Eyebrow { text: qsTr("Per game"); Layout.leftMargin: 4; Layout.topMargin: Theme.space14; Layout.bottomMargin: Theme.space6 }

            ColumnItem {
                objectName: "gameOverridesCard"
                selected: root.emu.gameSelected
                onClicked: root.emu.level = "game"
                RowLayout {
                    Layout.fillWidth: true
                    FbLabel { Layout.fillWidth: true; text: qsTr("Game overrides"); font.weight: Font.DemiBold; elide: Text.ElideRight }
                    FbLabel {
                        objectName: "gameOverridesChanged"
                        visible: root.emu.gameOverrideCount > 0
                        text: qsTr("%n game(s)", "", root.emu.gameOverrideCount)
                        font.pixelSize: Theme.fontMono
                        color: Theme.accent
                    }
                }
                FbLabel {
                    Layout.fillWidth: true
                    text: qsTr("Core and options for a single game")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontMeta
                    elide: Text.ElideRight
                }
            }

            Item { Layout.fillHeight: true }
        }

        // Main
        Flickable {
            id: flick
            objectName: "emulationScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: content.implicitHeight + 64
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }

            ColumnLayout {
                id: content
                x: 36
                y: 28
                width: flick.width - 72   // grows with the window; descriptions are capped per row (SettingsRow)
                spacing: 20

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space16
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 2
                        FbLabel {
                            objectName: "emulationTitle"
                            Layout.fillWidth: true
                            text: root.emu.defaultsSelected ? qsTr("Defaults for all systems")
                                  : root.emu.gameSelected ? (root.emu.selectedGame !== "" ? root.emu.game.title : qsTr("Game overrides"))
                                  : (root.emu.system.name !== undefined
                                     ? (root.emu.system.coreVersion !== ""
                                        ? qsTr("%1 · %2 %3").arg(root.emu.system.name).arg(root.emu.system.coreName).arg(root.emu.system.coreVersion)
                                        : qsTr("%1 · %2").arg(root.emu.system.name).arg(root.emu.system.coreName))
                                     : qsTr("Emulation"))
                            font.pixelSize: Theme.fontPage
                            font.weight: Font.DemiBold
                            font.letterSpacing: -0.4
                            elide: Text.ElideRight
                        }
                        FbLabel {
                            Layout.fillWidth: true
                            text: root.emu.defaultsSelected
                                  ? qsTr("Systems use these unless you change them there. Saved on this device only.")
                                  : root.emu.gameSelected
                                    ? qsTr("Only what you change here applies to this game; everything else follows the system. Saved on this device only.")
                                    : qsTr("Only options this core reports. Saved on this device only.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                    }
                    FbField {
                        objectName: "emulationSearch"
                        Layout.preferredWidth: 260
                        Layout.minimumWidth: 120
                        implicitHeight: 36
                        Layout.alignment: Qt.AlignTop
                        font.family: Qt.application.font.family
                        placeholderText: qsTr("Search settings…")
                        text: root.emu.searchText
                        onTextChanged: root.emu.searchText = text
                    }
                }

                // Per-game section: pick a game from the library.
                ColumnLayout {
                    objectName: "gamePicker"
                    visible: root.emu.gameSelected
                    Layout.fillWidth: true
                    spacing: Theme.space8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space10
                        FbLabel { text: qsTr("Game"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        FbSelect {
                            id: gameSelect
                            objectName: "gameSelect"
                            Layout.fillWidth: true
                            Layout.maximumWidth: 460
                            enabled: root.emu.games.length > 0
                            displayText: root.emu.selectedGame === "" ? qsTr("Pick a game…") : root.emu.game.label
                            model: root.emu.games
                            current: root.emu.selectedGame
                            onPicked: value => root.emu.selectGame(value)
                        }
                        FbLabel {
                            objectName: "gameChangedCount"
                            visible: root.emu.selectedGame !== "" && root.emu.game.changedCount > 0
                            text: qsTr("%1 changed").arg(root.emu.game.changedCount)
                            font.pixelSize: Theme.fontMono
                            color: Theme.accent
                        }
                    }
                    FbLabel {
                        objectName: "gamePickerHint"
                        visible: root.emu.selectedGame === ""
                        Layout.fillWidth: true
                        text: root.emu.games.length > 0 ? qsTr("Choose a game to give it its own core or options. Games without changes follow their system.")
                                                        : qsTr("No games in the library yet.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontMeta
                        wrapMode: Text.WordWrap
                    }
                }

                // Category chips + Reset N changed. The chips never wrap or widen the column: what does not fit collapses
                // into a "+N more ▾" chip with a menu (decisions.md ag).
                RowLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: Theme.space8
                    visible: root.emu.categories.length > 0
                    Item {
                        id: chipBar
                        objectName: "categoryChips"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredHeight: 32
                        readonly property var cats: root.emu.categories
                        FontMetrics { id: chipMetrics; font.pixelSize: Theme.fontSmall; font.weight: Font.Medium }
                        function chipWidth(t: string): real { return Math.ceil(chipMetrics.advanceWidth(t)) + 32 }
                        function moreText(n: int): string { return qsTr("+%1 more ▾").arg(n) }
                        // Number of category chips that fit next to "All" (and the "+N more" chip when some are left out).
                        readonly property int shown: {
                            const n = chipBar.cats.length
                            let used = chipBar.chipWidth(qsTr("All"))
                            let k = 0
                            for (; k < n; ++k) {
                                const need = used + Theme.space8 + chipBar.chipWidth(chipBar.cats[k])
                                const rest = n - k - 1
                                const reserve = rest > 0 ? Theme.space8 + chipBar.chipWidth(chipBar.moreText(rest)) : 0
                                if (need + reserve > chipBar.width) {
                                    break
                                }
                                used = need
                            }
                            return k
                        }
                        readonly property bool collapsed: chipBar.shown < chipBar.cats.length
                        readonly property bool hiddenActive: root.emu.categoryFilter !== "" && chipBar.cats.indexOf(root.emu.categoryFilter) >= chipBar.shown

                        Row {
                            spacing: Theme.space8
                            FbChip {
                                objectName: "categoryAll"
                                text: qsTr("All")
                                active: root.emu.categoryFilter === ""
                                onClicked: root.emu.categoryFilter = ""
                            }
                            Repeater {
                                model: chipBar.cats
                                delegate: FbChip {
                                    required property string modelData
                                    required property int index
                                    objectName: "category_" + modelData
                                    visible: index < chipBar.shown
                                    text: modelData
                                    active: root.emu.categoryFilter === modelData
                                    onClicked: root.emu.categoryFilter = modelData
                                }
                            }
                            FbChip {
                                id: moreChip
                                objectName: "categoryMore"
                                visible: chipBar.collapsed
                                text: chipBar.moreText(chipBar.cats.length - chipBar.shown)
                                active: chipBar.hiddenActive
                                onClicked: overflowMenu.opened ? overflowMenu.close() : overflowMenu.open()
                            }
                        }
                        Popup {
                            id: overflowMenu
                            parent: moreChip
                            y: moreChip.height + 4
                            width: 220
                            padding: 4
                            contentItem: Column {
                                spacing: 0
                                Repeater {
                                    model: chipBar.cats
                                    delegate: Rectangle {
                                        id: ovItem
                                        required property string modelData
                                        required property int index
                                        objectName: "categoryOverflow_" + modelData
                                        visible: index >= chipBar.shown
                                        width: parent ? parent.width : 0
                                        height: visible ? 30 : 0
                                        radius: Theme.radius5
                                        color: root.emu.categoryFilter === modelData ? Theme.borderInput : (ovHover.hovered ? Theme.surfaceRaised : "transparent")
                                        Text {
                                            anchors.fill: parent
                                            anchors.leftMargin: 8
                                            anchors.rightMargin: 8
                                            text: ovItem.modelData
                                            elide: Text.ElideRight
                                            verticalAlignment: Text.AlignVCenter
                                            font.pixelSize: Theme.fontSmall
                                            color: Theme.text
                                        }
                                        HoverHandler { id: ovHover }
                                        TapHandler {
                                            onTapped: {
                                                root.emu.categoryFilter = ovItem.modelData
                                                overflowMenu.close()
                                            }
                                        }
                                    }
                                }
                            }
                            background: Rectangle {
                                radius: Theme.radius8
                                color: Theme.popupBg
                                border.width: 1
                                border.color: Theme.borderGroup
                            }
                        }
                    }
                    FbButton {
                        objectName: "resetAllChanged"
                        Layout.alignment: Qt.AlignTop
                        visible: root.emu.changedCount > 0
                        kind: "link"
                        font.underline: true
                        text: qsTr("Reset %1 changed").arg(root.emu.changedCount)
                        onClicked: root.emu.resetAllChanged()
                    }
                }

                Rectangle {
                    objectName: "restartHint"
                    visible: root.emu.restartHint
                    Layout.fillWidth: true
                    implicitHeight: hintText.implicitHeight + 20
                    radius: Theme.radius8
                    color: Theme.accentChipBg
                    FbLabel {
                        id: hintText
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Some changes apply after the next game start.")
                        color: Theme.accent
                        font.pixelSize: Theme.fontSmall
                    }
                }

                FbLabel {
                    visible: root.emu.gameRunning
                    objectName: "gameRunningNote"
                    Layout.fillWidth: true
                    text: qsTr("A game is running. Changes apply the next time a game starts.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontMeta
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: root.emu.groups
                    delegate: ColumnLayout {
                        id: group
                        required property var modelData
                        objectName: "optionGroup_" + modelData.id
                        Layout.fillWidth: true
                        spacing: 0

                        FbLabel {
                            objectName: "coreNoteHint"
                            visible: group.modelData.note !== ""
                            Layout.fillWidth: true
                            Layout.bottomMargin: Theme.space10
                            text: group.modelData.note
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontMeta
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: group.modelData.options
                            delegate: ColumnLayout {
                                id: opt
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 0
                                // Category heading when the category changes.
                                ColumnLayout {
                                    visible: opt.modelData.category !== "" && (opt.index === 0 || group.modelData.options[opt.index - 1].category !== opt.modelData.category)
                                    Layout.fillWidth: true
                                    Layout.topMargin: opt.index === 0 ? 0 : 24
                                    spacing: Theme.space8
                                    Eyebrow { objectName: "categoryTitle_" + opt.modelData.category; text: opt.modelData.category }
                                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderCard }
                                }
                                SettingsRow {
                                    objectName: "optionRow_" + opt.modelData.key
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    idKey: opt.modelData.key
                                    controlName: "optionSelect_" + opt.modelData.key
                                    toggleName: "optionToggle_" + opt.modelData.key
                                    label: opt.modelData.label
                                    description: opt.modelData.description
                                    badge: opt.modelData.restart ? qsTr("applies on next start") : ""
                                    disabledReason: opt.modelData.disabledReason || ""
                                    values: opt.modelData.values
                                    current: opt.modelData.value
                                    changed: opt.modelData.isSet
                                    onValuePicked: value => root.emu.setOption(opt.modelData.key, value)
                                    onResetRequested: root.emu.resetOption(opt.modelData.key)
                                }
                            }
                        }
                    }
                }

                FbLabel {
                    visible: root.emu.defaultsSelected
                    objectName: "globalCoreHint"
                    Layout.fillWidth: true
                    text: qsTr("Options of the core are set per system, because their keys belong to the core.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontMeta
                    wrapMode: Text.WordWrap
                }
                FbLabel {
                    visible: !root.emu.defaultsSelected && root.emu.lockedCount > 0
                    objectName: "lockedHint"
                    Layout.fillWidth: true
                    text: qsTr("%n option(s) of the core are controlled by FrameBeam (render mode, screen layout, on-screen display, firmware) and are not shown.", "", root.emu.lockedCount)
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontMeta
                    wrapMode: Text.WordWrap
                }
                FbLabel {
                    objectName: "noMatchHint"
                    visible: (root.emu.searchText !== "" || root.emu.categoryFilter !== "") && root.emu.groups.length > 0 && !root.hasVisibleOptions
                    Layout.fillWidth: true
                    text: qsTr("No setting matches.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                }
            }
        }
    }

    readonly property bool hasVisibleOptions: {
        const g = root.emu.groups
        for (let i = 0; i < g.length; ++i) {
            if (g[i].options.length > 0) {
                return true
            }
        }
        return false
    }
}
