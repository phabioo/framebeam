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
    // Control kind of an option by its values: toggle (off/on), segment (short lists) or select.
    function controlKind(values: var): string {
        if (values.length === 2) {
            const v = [values[0].value, values[1].value].join("|")
            if (["off|on", "disabled|enabled", "false|true", "0|1"].indexOf(v) >= 0) {
                return "toggle"
            }
        }
        let chars = 0
        for (let i = 0; i < values.length; ++i) {
            chars += (values[i].label || "").length
        }
        return values.length >= 2 && values.length <= 4 && chars <= 38 ? "segment" : "select"
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
                    selected: !root.emu.defaultsSelected && modelData.id === root.emu.selectedSystem
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

            Item { Layout.fillHeight: true }

            // Placeholder (dashed)
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: moreCol.implicitHeight + 24
                radius: Theme.radius8
                color: "transparent"
                border.width: 1
                border.color: Theme.borderInput
                ColumnLayout {
                    id: moreCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 3
                    FbLabel { text: qsTr("Per-game settings"); color: Theme.textMuted; font.weight: Font.Medium }
                    FbLabel {
                        Layout.fillWidth: true
                        text: qsTr("Coming later. Set from a game's page in the Library.")
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontMeta
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // Main
        Flickable {
            id: flick
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
                width: Math.min(900, flick.width - 72)
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

                // Category chips + Reset N changed
                // Category chips + Reset N changed. The chips wrap (Flow) instead of sitting in a RowLayout: a row of chips
                // cannot shrink, so with many categories or wider platform fonts its minimum width would widen the whole
                // content column and push the option controls out of the window.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: Theme.space8
                    visible: root.emu.categories.length > 0
                    Flow {
                        id: chipFlow
                        objectName: "categoryChips"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredHeight: childrenRect.height
                        spacing: Theme.space8
                        FbChip {
                            objectName: "categoryAll"
                            text: qsTr("All")
                            active: root.emu.categoryFilter === ""
                            onClicked: root.emu.categoryFilter = ""
                        }
                        Repeater {
                            model: root.emu.categories
                            delegate: FbChip {
                                required property string modelData
                                objectName: "category_" + modelData
                                text: modelData
                                active: root.emu.categoryFilter === modelData
                                onClicked: root.emu.categoryFilter = modelData
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
                                readonly property string kind: root.controlKind(opt.modelData.values)
                                Layout.fillWidth: true
                                spacing: 0
                                // Category heading when the category changes.
                                ColumnLayout {
                                    visible: opt.modelData.category !== "" && (opt.index === 0 || group.modelData.options[opt.index - 1].category !== opt.modelData.category)
                                    Layout.fillWidth: true
                                    Layout.topMargin: opt.index === 0 ? 0 : Theme.space20
                                    spacing: Theme.space8
                                    Eyebrow { objectName: "categoryTitle_" + opt.modelData.category; text: opt.modelData.category }
                                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderRow }
                                }
                                GridLayout {
                                    id: optRow
                                    objectName: "optionRow_" + opt.modelData.key
                                    // Narrow window: the reset column moves below the control.
                                    readonly property bool narrow: content.width < 640
                                    columns: optRow.narrow ? 2 : 3
                                    columnSpacing: Theme.space20
                                    rowSpacing: 4
                                    Layout.fillWidth: true
                                    Layout.topMargin: 13
                                    Layout.bottomMargin: 13

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        Layout.alignment: Qt.AlignTop
                                        Layout.rowSpan: optRow.narrow ? 2 : 1
                                        spacing: 3
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            spacing: Theme.space8
                                            Rectangle {
                                                objectName: "changedDot_" + opt.modelData.key
                                                visible: opt.modelData.isSet
                                                Layout.preferredWidth: 7
                                                Layout.preferredHeight: 7
                                                radius: 3.5
                                                color: Theme.accent
                                            }
                                            FbLabel { Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight; text: opt.modelData.label; font.weight: Font.Medium }
                                            Rectangle {
                                                visible: opt.modelData.restart
                                                objectName: "restartBadge_" + opt.modelData.key
                                                implicitWidth: badgeText.implicitWidth + 14
                                                implicitHeight: 20
                                                radius: Theme.radius4
                                                color: Theme.neutralPillBg
                                                Text {
                                                    id: badgeText
                                                    anchors.centerIn: parent
                                                    text: qsTr("applies on next start")
                                                    font.pixelSize: Theme.fontMono
                                                    color: Theme.textMuted
                                                }
                                            }
                                        }
                                        FbLabel {
                                            visible: opt.modelData.description !== ""
                                            Layout.fillWidth: true
                                            text: opt.modelData.description
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.fontMeta
                                            wrapMode: Text.WordWrap
                                            maximumLineCount: 3
                                            elide: Text.ElideRight
                                        }
                                    }

                                    // Control: toggle, segment or select
                                    Item {
                                        Layout.alignment: Qt.AlignTop
                                        Layout.preferredWidth: opt.kind === "select" ? 200 : ctl.implicitWidth
                                        Layout.preferredHeight: 34
                                        implicitWidth: ctl.implicitWidth
                                        implicitHeight: 34
                                        Loader {
                                            id: ctl
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            sourceComponent: opt.kind === "toggle" ? toggleComp : (opt.kind === "segment" ? segmentComp : selectComp)
                                        }
                                    }

                                    RowLayout {
                                        Layout.preferredWidth: optRow.narrow ? 200 : 90
                                        Layout.alignment: Qt.AlignTop
                                        Layout.topMargin: optRow.narrow ? 0 : 7
                                        FbLabel {
                                            objectName: "optionOrigin_" + opt.modelData.key
                                            visible: !opt.modelData.isSet
                                            text: qsTr("Default")
                                            color: Theme.textDisabled
                                            font.pixelSize: Theme.fontSmall
                                        }
                                        FbButton {
                                            visible: opt.modelData.isSet
                                            objectName: "optionReset_" + opt.modelData.key
                                            kind: "link"
                                            font.underline: true
                                            text: qsTr("Reset")
                                            font.pixelSize: Theme.fontSmall
                                            onClicked: root.emu.resetOption(opt.modelData.key)
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderRow }
                                Component {
                                    id: toggleComp
                                    FbToggle {
                                        objectName: "optionToggle_" + opt.modelData.key
                                        checked: opt.modelData.values.length === 2 && opt.modelData.value === opt.modelData.values[1].value
                                        onToggled: root.emu.setOption(opt.modelData.key, checked ? opt.modelData.values[1].value : opt.modelData.values[0].value)
                                    }
                                }
                                Component {
                                    id: segmentComp
                                    FbSegment {
                                        objectName: "optionSelect_" + opt.modelData.key
                                        options: opt.modelData.values.map(function (v) { return { value: v.value, label: v.label } })
                                        current: opt.modelData.value
                                        onPicked: value => root.emu.setOption(opt.modelData.key, value)
                                    }
                                }
                                Component {
                                    id: selectComp
                                    FbSelect {
                                        objectName: "optionSelect_" + opt.modelData.key
                                        implicitWidth: 200
                                        model: opt.modelData.values
                                        current: opt.modelData.value
                                        onPicked: value => root.emu.setOption(opt.modelData.key, value)
                                    }
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
