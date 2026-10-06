import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// 3e: Emulation. System list | settings of the selected system (FrameBeam options + core options reported by the core).
// Settings are local; only explicitly set values override (Global -> System / Core -> Game Override).
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
            Layout.preferredWidth: 232
            player: root.player
        }

        // System list
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 260
            color: Theme.bgPanel
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.borderSidebar
            }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                anchors.topMargin: 24
                spacing: 8

                Eyebrow { text: qsTr("Emulation"); Layout.leftMargin: 4; Layout.bottomMargin: 4 }

                Repeater {
                    model: root.emu.systems
                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        objectName: "systemCard_" + modelData.id
                        readonly property bool selected: modelData.id === root.emu.selectedSystem
                        Layout.fillWidth: true
                        implicitHeight: cardCol.implicitHeight + 24
                        radius: 8
                        color: card.selected ? Theme.surfaceRaised : "transparent"
                        border.width: card.selected ? 1 : 0
                        border.color: Theme.borderCard
                        ColumnLayout {
                            id: cardCol
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 3
                            FbLabel { text: card.modelData.name; font.weight: Font.Medium }
                            FbLabel {
                                objectName: "systemCore_" + card.modelData.id
                                text: card.modelData.coreVersion !== ""
                                      ? qsTr("%1 · %2").arg(card.modelData.coreName).arg(card.modelData.coreVersion)
                                      : card.modelData.coreName
                                color: Theme.textMuted
                                font.pixelSize: 12
                            }
                            RowLayout {
                                spacing: 6
                                StatusDot { tone: card.modelData.readyTone }
                                FbLabel {
                                    objectName: "systemReady_" + card.modelData.id
                                    text: card.modelData.readyText
                                    color: root.toneColor(card.modelData.readyTone)
                                    font.pixelSize: 12
                                }
                            }
                            FbLabel {
                                objectName: "systemFirmware_" + card.modelData.id
                                text: card.modelData.firmwareText
                                color: root.toneColor(card.modelData.firmwareTone)
                                font.pixelSize: 12
                            }
                        }
                        TapHandler { onTapped: root.emu.selectSystem(card.modelData.id) }
                    }
                }

                // Placeholder
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: moreCol.implicitHeight + 24
                    radius: 8
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.borderButton
                    ColumnLayout {
                        id: moreCol
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 3
                        FbLabel { text: qsTr("More systems"); color: Theme.textMuted; font.weight: Font.Medium }
                        FbLabel {
                            Layout.fillWidth: true
                            text: qsTr("Will be added later via core, manifest and profiles.")
                            color: Theme.textFaint
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                Item { Layout.fillHeight: true }
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

            ColumnLayout {
                id: content
                x: 28
                y: 28
                width: Math.min(900, flick.width - 56)
                spacing: 22

                ColumnLayout {
                    spacing: 2
                    FbLabel {
                        objectName: "emulationTitle"
                        text: root.emu.system.name !== undefined
                              ? qsTr("%1 · %2").arg(root.emu.system.name).arg(root.emu.system.coreName)
                              : qsTr("Emulation")
                        font.pixelSize: 26
                        font.weight: Font.DemiBold
                        font.letterSpacing: -0.4
                    }
                    FbLabel {
                        text: qsTr("Settings apply locally to this device")
                        color: Theme.textMuted
                        font.pixelSize: 13
                    }
                }

                // Level switch
                RowLayout {
                    spacing: 14
                    FbSegment {
                        objectName: "levelSegment"
                        options: [
                            { value: "global", label: qsTr("Global"), name: "levelGlobal" },
                            { value: "system", label: qsTr("System / Core"), name: "levelSystem" },
                            { value: "game", label: qsTr("Game Override · later"), name: "levelGame", disabled: true }
                        ]
                        current: root.emu.level
                        onPicked: value => root.emu.level = value
                    }
                    FbLabel {
                        Layout.fillWidth: true
                        text: qsTr("Global → System/Core → Game Override · only explicitly set values override")
                        color: Theme.textFaint
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }

                FbLabel {
                    visible: root.emu.gameRunning
                    objectName: "gameRunningNote"
                    Layout.fillWidth: true
                    text: qsTr("A game is running. Changes apply the next time a game starts.")
                    color: Theme.warn
                    font.pixelSize: 12
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

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.bottomMargin: 10
                            spacing: 2
                            FbLabel { text: group.modelData.title; font.pixelSize: 15; font.weight: Font.DemiBold }
                            FbLabel { text: group.modelData.subtitle; color: Theme.textMuted; font.pixelSize: 12 }
                            FbLabel {
                                visible: group.modelData.note !== ""
                                Layout.fillWidth: true
                                text: group.modelData.note
                                color: Theme.warn
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderRow }

                        Repeater {
                            model: group.modelData.options
                            delegate: ColumnLayout {
                                id: opt
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: 0
                                // Category heading when the category changes (core options only).
                                Eyebrow {
                                    visible: opt.modelData.category !== "" && (opt.index === 0 || group.modelData.options[opt.index - 1].category !== opt.modelData.category)
                                    Layout.topMargin: 14
                                    Layout.bottomMargin: 2
                                    text: opt.modelData.category
                                }
                                GridLayout {
                                    id: optRow
                                    objectName: "optionRow_" + opt.modelData.key
                                    // Narrow window: the origin moves below the select instead of a third column.
                                    readonly property bool narrow: content.width < 640
                                    columns: optRow.narrow ? 2 : 3
                                    columnSpacing: 20
                                    rowSpacing: 4
                                    Layout.fillWidth: true
                                    Layout.topMargin: 10
                                    Layout.bottomMargin: 10

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignTop
                                        Layout.rowSpan: optRow.narrow ? 2 : 1
                                        spacing: 3
                                        RowLayout {
                                            spacing: 8
                                            FbLabel { text: opt.modelData.label; font.weight: Font.Medium }
                                            Rectangle {
                                                visible: opt.modelData.restart
                                                objectName: "restartBadge_" + opt.modelData.key
                                                implicitWidth: badgeText.implicitWidth + 14
                                                implicitHeight: 20
                                                radius: 10
                                                color: Theme.warnBg
                                                Text {
                                                    id: badgeText
                                                    anchors.centerIn: parent
                                                    text: qsTr("Restart required")
                                                    font.pixelSize: 11
                                                    color: Theme.warn
                                                }
                                            }
                                        }
                                        FbLabel {
                                            visible: opt.modelData.description !== ""
                                            Layout.fillWidth: true
                                            text: opt.modelData.description
                                            color: Theme.textMuted
                                            font.pixelSize: 12
                                            wrapMode: Text.WordWrap
                                            maximumLineCount: 3
                                            elide: Text.ElideRight
                                        }
                                    }
                                    FbSelect {
                                        objectName: "optionSelect_" + opt.modelData.key
                                        Layout.preferredWidth: 220
                                        Layout.minimumWidth: 220
                                        Layout.maximumWidth: 220
                                        Layout.alignment: Qt.AlignTop
                                        model: opt.modelData.values
                                        current: opt.modelData.value
                                        onPicked: value => root.emu.setOption(opt.modelData.key, value)
                                    }
                                    RowLayout {
                                        Layout.preferredWidth: optRow.narrow ? 220 : 200
                                        Layout.minimumWidth: optRow.narrow ? 220 : 200
                                        Layout.maximumWidth: optRow.narrow ? 220 : 200
                                        Layout.alignment: Qt.AlignTop
                                        Layout.topMargin: optRow.narrow ? 0 : 7
                                        spacing: 8
                                        FbLabel {
                                            objectName: "optionOrigin_" + opt.modelData.key
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            text: opt.modelData.isSet ? "● " + opt.modelData.origin : opt.modelData.origin
                                            color: opt.modelData.isSet ? Theme.warn : Theme.textMuted
                                            font.pixelSize: 12
                                        }
                                        FbButton {
                                            visible: opt.modelData.isSet
                                            objectName: "optionReset_" + opt.modelData.key
                                            kind: "link"
                                            text: qsTr("reset")
                                            font.pixelSize: 12
                                            onClicked: root.emu.resetOption(opt.modelData.key)
                                        }
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderRow }
                            }
                        }
                    }
                }

                FbLabel {
                    visible: root.emu.level === "global"
                    objectName: "globalCoreHint"
                    Layout.fillWidth: true
                    text: qsTr("Options of the core are set per System / Core, because their keys belong to the core.")
                    color: Theme.textMuted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                FbLabel {
                    visible: root.emu.level === "system" && root.emu.lockedCount > 0
                    objectName: "lockedHint"
                    Layout.fillWidth: true
                    text: qsTr("%n option(s) of the core are controlled by FrameBeam (render mode, screen layout, on-screen display, firmware) and are not shown.", "", root.emu.lockedCount)
                    color: Theme.textFaint
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
