import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

// 3f: Controllers. Devices | profile and mapping table | input test. Profiles stay local on this device.
Rectangle {
    id: root
    required property PlayerController player
    readonly property var ctl: root.player.controllers
    readonly property bool isMouse: root.ctl.device.kind === "mouse"
    property bool renaming: false
    property bool confirmDelete: false
    property string tab: "buttons"  // "buttons" | "hotkeys" (hotkeys are global, independent of the selected device)
    readonly property bool hotkeysTab: root.tab === "hotkeys"
    // Narrow main column (input test open on a small window): tighter mapping table.
    readonly property bool compact: content.width < 520
    readonly property int mapWidth: compact ? 130 : 170
    readonly property int mapMinWidth: compact ? 72 : 100
    readonly property int targetWidth: compact ? 56 : 80
    readonly property int actionWidth: compact ? 48 : 60
    // Header: selects beside the title on a wide column, below it on a narrow one.
    readonly property bool wideHeader: content.width >= 640
    // Buttons table (grid 14 | flex | 240 | 72): the mapping field and the action cell give way on a narrow column.
    readonly property bool tight: content.width < 480
    readonly property int padActionWidth: tight ? 56 : 72
    readonly property int hotkeyGapWidth: compact ? 0 : root.targetWidth  // empty third column of the Hotkeys table
    color: Theme.bg

    onVisibleChanged: if (visible) keyScope.forceActiveFocus()
    Connections {
        target: root.ctl
        function onSelectionChanged() { root.renaming = false; root.confirmDelete = false }
        function onProfilesChanged() { root.confirmDelete = false }
    }

    // Keys: capture while "Press a button…" is shown on a keyboard profile, else feed the input test.
    FocusScope {
        id: keyScope
        anchors.fill: parent
        focus: root.visible
        Keys.onPressed: event => {
            if (root.renaming) { return }
            if (root.ctl.captureKey(event.key)) {
                event.accepted = true
            } else if (!event.isAutoRepeat) {
                root.ctl.testKey(event.key, true)
            }
        }
        Keys.onReleased: event => {
            if (!event.isAutoRepeat) { root.ctl.testKey(event.key, false) }
        }
        onActiveFocusChanged: if (!activeFocus) root.ctl.clearTestKeys()

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Sidebar {
                Layout.fillHeight: true
                Layout.preferredWidth: 232
                player: root.player
            }

            // Devices
            ShellColumn {
                Layout.fillHeight: true
                Layout.preferredWidth: Theme.columnWidth
                title: qsTr("Devices")

                Repeater {
                    model: root.ctl.devices
                    delegate: ColumnItem {
                        id: dev
                        required property var modelData
                        objectName: "device_" + modelData.key
                        selected: modelData.key === root.ctl.selectedDevice
                        onClicked: root.ctl.selectDevice(dev.modelData.key)
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            FbLabel {
                                Layout.fillWidth: true
                                text: dev.modelData.name
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            FbMono {
                                objectName: "deviceSlot_" + dev.modelData.key
                                text: dev.modelData.slot
                                font.pixelSize: Theme.fontMono
                                color: Theme.textMeta
                            }
                        }
                        FbLabel {
                            objectName: "deviceStatus_" + dev.modelData.key
                            Layout.fillWidth: true
                            text: (dev.modelData.connected ? "● " : "") + dev.modelData.status
                                  + (dev.modelData.profile !== "" ? " · " + dev.modelData.profile : "")
                            color: dev.modelData.connected ? Theme.ok : Theme.textFaint
                            font.pixelSize: Theme.fontMeta
                            elide: Text.ElideRight
                        }
                    }
                }

                FbLabel {
                    visible: !root.ctl.gamepadAvailable
                    objectName: "gamepadNote"
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space8
                    text: root.ctl.gamepadNote
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontMeta
                    wrapMode: Text.WordWrap
                }
                FbLabel {
                    visible: root.ctl.gamepadAvailable && root.ctl.devices.length <= 2
                    objectName: "noGamepadHint"
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space8
                    text: qsTr("No gamepad connected. Connect one and it appears here (the first gamepad is P1).")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontMeta
                    wrapMode: Text.WordWrap
                }

                Item { Layout.fillHeight: true }
                FbLabel {
                    objectName: "profilesLocalFooter"
                    Layout.fillWidth: true
                    text: qsTr("Saved on this device only")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontMeta
                    wrapMode: Text.WordWrap
                }
            }

            // Main: profile and mapping
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
                    width: Math.min(760, flick.width - 56)
                    spacing: 18

                    GridLayout {
                        id: header
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        columns: root.wideHeader ? 2 : 1
                        columnSpacing: Theme.space16
                        rowSpacing: Theme.space12
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: Theme.space6
                            FbLabel {
                                objectName: "controllersTitle"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: root.hotkeysTab ? qsTr("Hotkeys")
                                      : root.ctl.device.name !== undefined ? root.ctl.device.name : qsTr("Controllers")
                                font.pixelSize: Theme.fontPage
                                font.weight: Font.DemiBold
                                font.letterSpacing: -0.4
                                elide: Text.ElideRight
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: Theme.space10
                                FbPill {
                                    objectName: "devicePill"
                                    visible: root.ctl.device.name !== undefined && !root.hotkeysTab
                                    tone: root.ctl.device.connected ? "ok" : "neutral"
                                    text: root.ctl.device.connected === true
                                          ? (root.ctl.device.slot !== "—" ? qsTr("Connected · %1").arg(root.ctl.device.slot) : qsTr("Connected"))
                                          : qsTr("Not connected")
                                }
                                FbLabel {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    elide: Text.ElideRight
                                    text: root.hotkeysTab ? qsTr("Player hotkeys · saved on this device only") : qsTr("Saved on this device only")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontMeta
                                }
                            }
                        }
                        RowLayout {
                            id: selects
                            objectName: "headerSelects"
                            visible: !root.isMouse && !root.hotkeysTab
                            Layout.alignment: Qt.AlignTop | (root.wideHeader ? Qt.AlignRight : Qt.AlignLeft)
                            Layout.minimumWidth: 0
                            spacing: Theme.space12
                            // Button labels: glyph set of this gamepad (Auto = detected type); saved per physical device.
                            ColumnLayout {
                                visible: root.ctl.device.kind === "gamepad"
                                Layout.preferredWidth: 170
                                Layout.minimumWidth: 110
                                spacing: 4
                                FbLabel { text: qsTr("Button labels"); color: Theme.textFaint; font.pixelSize: Theme.fontMono }
                                FbSelect {
                                    objectName: "labelSetSelect"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Layout.preferredHeight: 34
                                    model: root.ctl.labelChoices
                                    current: root.ctl.labelChoice
                                    onPicked: value => root.ctl.setLabelChoice(value)
                                }
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 180
                                Layout.minimumWidth: 110
                                spacing: 4
                                FbLabel { text: qsTr("Profile"); color: Theme.textFaint; font.pixelSize: Theme.fontMono }
                                FbSelect {
                                    objectName: "profileSelect"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Layout.preferredHeight: 34
                                    model: root.ctl.profiles
                                    current: root.ctl.profileId
                                    onPicked: value => root.ctl.selectProfile(value)
                                }
                            }
                        }
                    }
                    FbLabel {
                        objectName: "labelSetHelp"
                        visible: root.ctl.device.kind === "gamepad" && !root.hotkeysTab
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.minimumWidth: 0
                        Layout.topMargin: -8
                        text: qsTr("Auto uses the detected controller type. Saved per device.")
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontMeta
                        wrapMode: Text.WordWrap
                    }

                    // Tab chips + Reset N changed
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0  // chips + "Reset N changed" must not widen the whole column on narrow windows
                        spacing: Theme.space8
                        FbChip { objectName: "tabButtons"; text: qsTr("Buttons"); active: !root.hotkeysTab; onClicked: { root.ctl.cancelHotkeyCapture(); root.tab = "buttons" } }
                        FbChip { objectName: "tabHotkeys"; text: qsTr("Hotkeys"); active: root.hotkeysTab; onClicked: { root.ctl.cancelCapture(); root.tab = "hotkeys" } }
                        Item { Layout.fillWidth: true }
                        FbButton {
                            objectName: "resetChanged"
                            readonly property int n: root.hotkeysTab ? root.ctl.hotkeysChangedCount : root.ctl.changedCount
                            visible: n > 0 && (root.hotkeysTab || (!root.isMouse && !root.ctl.profileBuiltin))
                            kind: "link"
                            font.underline: true
                            text: root.compact ? qsTr("Reset %1").arg(n) : qsTr("Reset %1 changed").arg(n)
                            onClicked: root.hotkeysTab ? root.ctl.resetHotkeys() : root.ctl.resetProfile()
                        }
                    }

                    // Mouse: touch input (fixed)
                    FbLabel {
                        visible: root.isMouse && !root.hotkeysTab
                        objectName: "mouseNote"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.minimumWidth: 0
                        text: qsTr("The mouse is the %1 input: move it over the touch screen, left mouse button = touch. This mapping is fixed and has no profile.").arg(root.ctl.touchLabel)
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        visible: !root.isMouse && !root.hotkeysTab && root.ctl.profileBuiltin
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: Theme.space12
                        FbLabel {
                            objectName: "builtinNote"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: qsTr("Built-in profiles are read-only. Duplicate the profile to change the mapping.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontMeta
                            wrapMode: Text.WordWrap
                        }
                        FbButton {
                            objectName: "duplicateToEditButton"
                            text: qsTr("Duplicate to edit")
                            onClicked: root.ctl.duplicateProfile()
                        }
                    }

                    // Hotkeys table (global): ACTION | KEY | (empty) | Reset / Default
                    ColumnLayout {
                        visible: root.hotkeysTab
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: content.width
                        spacing: 0

                        FbLabel {
                            objectName: "hotkeysInfo"
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.minimumWidth: 0
                            Layout.bottomMargin: Theme.space12
                            text: qsTr("Player hotkeys work on the keyboard and are never sent to the game.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontMeta
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.bottomMargin: 6
                            spacing: Theme.space16
                            Eyebrow { Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0; text: qsTr("Action") }
                            Eyebrow { Layout.fillWidth: true; Layout.preferredWidth: root.mapWidth; Layout.minimumWidth: root.mapMinWidth; Layout.maximumWidth: root.mapWidth; text: qsTr("Key") }
                            Item { Layout.preferredWidth: root.hotkeyGapWidth; Layout.minimumWidth: root.hotkeyGapWidth; Layout.maximumWidth: root.hotkeyGapWidth }
                            Item { Layout.preferredWidth: root.actionWidth; Layout.minimumWidth: root.actionWidth; Layout.maximumWidth: root.actionWidth }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderRow }

                        Repeater {
                            model: root.ctl.hotkeyRows
                            delegate: ColumnLayout {
                                id: hrow
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 6
                                    Layout.bottomMargin: 6
                                    spacing: Theme.space16
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        Layout.minimumWidth: 0
                                        spacing: Theme.space8
                                        Rectangle {
                                            objectName: "hotkeyChangedDot_" + hrow.modelData.action
                                            visible: hrow.modelData.changed
                                            Layout.preferredWidth: 7
                                            Layout.preferredHeight: 7
                                            radius: 3.5
                                            color: Theme.accent
                                        }
                                        // Same structure as the Buttons table: one label directly in the cell.
                                        FbLabel { Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0; text: hrow.modelData.label; elide: Text.ElideRight }
                                    }
                                    Rectangle {
                                        objectName: "hotkeyField_" + hrow.modelData.action
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: root.mapWidth
                                        Layout.minimumWidth: root.mapMinWidth
                                        Layout.maximumWidth: root.mapWidth
                                        implicitHeight: 30
                                        radius: Theme.radius6
                                        color: Theme.surface
                                        border.width: hrow.modelData.listening ? 1.5 : 1
                                        border.color: hrow.modelData.listening ? Theme.accent : Theme.borderInput
                                        opacity: hrow.modelData.fixed ? 0.85 : 1
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 4
                                            FbLabel {
                                                objectName: "hotkeyText_" + hrow.modelData.action
                                                Layout.fillWidth: true
                                                Layout.preferredWidth: 1
                                                Layout.minimumWidth: 0
                                                text: hrow.modelData.listening ? qsTr("Press a key…") : hrow.modelData.key
                                                color: hrow.modelData.listening ? Theme.accent
                                                       : (hrow.modelData.set ? Theme.text : Theme.textFaint)
                                                font.pixelSize: Theme.fontSmall
                                                elide: Text.ElideRight
                                            }
                                            FbButton {
                                                visible: hrow.modelData.set && !hrow.modelData.fixed && !hrow.modelData.listening
                                                objectName: "hotkeyClear_" + hrow.modelData.action
                                                kind: "link"
                                                text: "×"
                                                onClicked: root.ctl.clearHotkey(hrow.modelData.action)
                                            }
                                        }
                                        TapHandler {
                                            enabled: !hrow.modelData.fixed
                                            onTapped: hrow.modelData.listening ? root.ctl.cancelHotkeyCapture() : root.ctl.beginHotkeyCapture(hrow.modelData.action)
                                        }
                                    }
                                    Item { Layout.preferredWidth: root.hotkeyGapWidth; Layout.minimumWidth: root.hotkeyGapWidth; Layout.maximumWidth: root.hotkeyGapWidth }
                                    RowLayout {
                                        Layout.preferredWidth: root.actionWidth
                                        Layout.minimumWidth: root.actionWidth
                                        Layout.maximumWidth: root.actionWidth
                                        FbButton {
                                            objectName: "hotkeyReset_" + hrow.modelData.action
                                            Layout.maximumWidth: root.actionWidth
                                            visible: hrow.modelData.changed
                                            kind: "link"
                                            font.underline: true
                                            font.pixelSize: Theme.fontMeta
                                            text: qsTr("Reset")
                                            onClicked: root.ctl.resetHotkey(hrow.modelData.action)
                                        }
                                        FbLabel {
                                            visible: !hrow.modelData.changed && !hrow.modelData.fixed
                                            text: qsTr("Default")
                                            font.pixelSize: Theme.fontMeta
                                            color: Theme.textDisabled
                                        }
                                        FbLabel {
                                            visible: hrow.modelData.fixed
                                            text: qsTr("Fixed")
                                            font.pixelSize: Theme.fontMeta
                                            color: Theme.textDisabled
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                }
                                FbLabel {
                                    objectName: "hotkeyConflict_" + hrow.modelData.action
                                    visible: hrow.modelData.conflictInput !== ""
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    Layout.minimumWidth: 0
                                    Layout.bottomMargin: 6
                                    text: qsTr("Also mapped to %1 in the keyboard profile · the hotkey wins").arg(hrow.modelData.conflictInput)
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontMeta
                                    elide: Text.ElideRight
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderRow }
                            }
                        }
                        FbLabel {
                            objectName: "hotkeyNote"
                            visible: root.ctl.hotkeyNote !== ""
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.minimumWidth: 0
                            Layout.topMargin: Theme.space8
                            text: root.ctl.hotkeyNote
                            color: Theme.warn
                            font.pixelSize: Theme.fontMeta
                            wrapMode: Text.WordWrap
                        }
                    }

                    // Mapping table: grid 14 | flex | 240 | 72 (dot | DS input | mapping | Reset / Default)
                    ColumnLayout {
                        visible: !root.isMouse && !root.hotkeysTab
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.bottomMargin: 6
                            spacing: Theme.space12
                            Item { Layout.preferredWidth: 14; Layout.minimumWidth: 14; Layout.maximumWidth: 14 }
                            Eyebrow {
                                Layout.fillWidth: true; Layout.preferredWidth: 90; Layout.minimumWidth: 0
                                text: root.ctl.systemLabel !== "" ? qsTr("%1 input").arg(root.ctl.systemLabel) : qsTr("Input")
                                font.letterSpacing: 0.66
                            }
                            Eyebrow { Layout.fillWidth: true; Layout.preferredWidth: root.tight ? 190 : 240; Layout.minimumWidth: 120; Layout.maximumWidth: 240; text: qsTr("Mapping"); font.letterSpacing: 0.66 }
                            Item { Layout.preferredWidth: root.padActionWidth; Layout.minimumWidth: root.padActionWidth; Layout.maximumWidth: root.padActionWidth }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderRow }

                        Repeater {
                            model: root.ctl.rows
                            delegate: ColumnLayout {
                                id: mrow
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 0
                                RowLayout {
                                    id: mline
                                    objectName: "mapRow_" + mrow.modelData.input
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Layout.topMargin: 7
                                    Layout.bottomMargin: 7
                                    spacing: Theme.space12
                                    Item {
                                        Layout.preferredWidth: 14
                                        Layout.minimumWidth: 14
                                        Layout.maximumWidth: 14
                                        Layout.preferredHeight: 14
                                        Rectangle {
                                            objectName: "changedDot_" + mrow.modelData.input
                                            visible: mrow.modelData.changed
                                            anchors.centerIn: parent
                                            width: 7
                                            height: 7
                                            radius: 3.5
                                            color: Theme.accent
                                        }
                                    }
                                    FbLabel {
                                        objectName: "mapLabel_" + mrow.modelData.input
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 90
                                        Layout.minimumWidth: 0
                                        text: mrow.modelData.label
                                        elide: Text.ElideRight
                                    }
                                    Rectangle {
                                        id: field
                                        objectName: "mapField_" + mrow.modelData.input
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: root.tight ? 190 : 240
                                        Layout.minimumWidth: 120
                                        Layout.maximumWidth: 240
                                        implicitHeight: 32
                                        radius: Theme.radius6
                                        color: Theme.surface
                                        border.width: mrow.modelData.listening ? 1.5 : 1
                                        border.color: mrow.modelData.listening ? Theme.accent : Theme.borderInput
                                        opacity: root.ctl.profileBuiltin ? 0.85 : 1
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 5
                                            anchors.rightMargin: 6
                                            spacing: 6
                                            Row {
                                                objectName: "mapGlyphs_" + mrow.modelData.input
                                                visible: !mrow.modelData.listening && mrow.modelData.glyphs.length > 0
                                                Layout.minimumWidth: 0
                                                Layout.maximumWidth: field.width * 0.7
                                                clip: true
                                                spacing: 4
                                                Repeater {
                                                    model: mrow.modelData.glyphs
                                                    delegate: PadGlyph {
                                                        required property string modelData
                                                        size: 22
                                                        g: modelData
                                                    }
                                                }
                                            }
                                            FbLabel {
                                                objectName: "mapText_" + mrow.modelData.input
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                                Layout.leftMargin: mrow.modelData.mapped || mrow.modelData.listening ? 0 : 5
                                                text: mrow.modelData.listening
                                                      ? (root.ctl.device.kind === "keyboard" ? qsTr("Press a key…") : qsTr("Press a button…"))
                                                      : mrow.modelData.bindingName
                                                color: mrow.modelData.listening ? Theme.accent
                                                       : (mrow.modelData.mapped ? Theme.textSecondary : Theme.textFaint)
                                                font.pixelSize: Theme.fontSmall
                                                elide: Text.ElideRight
                                            }
                                            FbButton {
                                                visible: mrow.modelData.mapped && !root.ctl.profileBuiltin && !mrow.modelData.listening
                                                objectName: "mapClear_" + mrow.modelData.input
                                                kind: "link"
                                                text: "×"
                                                onClicked: root.ctl.clearBinding(mrow.modelData.input)
                                            }
                                        }
                                        TapHandler {
                                            // Built-in profile: duplicate to a user profile first, then capture on the copy.
                                            onTapped: root.ctl.profileBuiltin ? root.ctl.duplicateAndCapture(mrow.modelData.input)
                                                      : mrow.modelData.listening ? root.ctl.cancelCapture() : root.ctl.beginCapture(mrow.modelData.input)
                                        }
                                    }
                                    RowLayout {
                                        objectName: "mapAction_" + mrow.modelData.input
                                        Layout.preferredWidth: root.padActionWidth
                                        Layout.minimumWidth: root.padActionWidth
                                        Layout.maximumWidth: root.padActionWidth
                                        FbButton {
                                            objectName: "mapReset_" + mrow.modelData.input
                                            Layout.minimumWidth: 0
                                            visible: mrow.modelData.changed
                                            kind: "link"
                                            font.underline: true
                                            font.pixelSize: Theme.fontMeta
                                            text: qsTr("Reset")
                                            onClicked: root.ctl.resetBinding(mrow.modelData.input)
                                        }
                                        FbLabel {
                                            visible: !mrow.modelData.changed
                                            text: qsTr("Default")
                                            font.pixelSize: Theme.fontMeta
                                            color: Theme.textDisabled
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderRow }
                            }
                        }

                        // Lid: only for input profiles that support it (none of the nds profiles does yet).
                        Loader {
                            active: root.ctl.supportsLid
                            Layout.fillWidth: true
                            sourceComponent: RowLayout {
                                spacing: Theme.space12
                                Item { Layout.preferredWidth: 14 }
                                FbLabel { Layout.fillWidth: true; Layout.preferredWidth: 90; Layout.minimumWidth: 0; text: qsTr("Close lid") }
                                FbLabel { Layout.preferredWidth: root.tight ? 190 : 240; Layout.minimumWidth: 120; Layout.maximumWidth: 240; Layout.fillWidth: true; text: qsTr("Unassigned"); color: Theme.textFaint }
                                Item { Layout.preferredWidth: root.padActionWidth }
                            }
                        }
                    }

                    // Profile actions
                    Flow {
                        visible: !root.isMouse && !root.hotkeysTab
                        Layout.fillWidth: true
                        spacing: 10
                        FbButton {
                            objectName: "resetProfileButton"
                            implicitHeight: 38
                            text: qsTr("Reset profile to default")
                            enabled: !root.ctl.profileBuiltin
                            onClicked: root.ctl.resetProfile()
                        }
                        FbButton {
                            objectName: "duplicateProfileButton"
                            implicitHeight: 38
                            text: qsTr("Duplicate profile")
                            onClicked: root.ctl.duplicateProfile()
                        }
                        FbButton {
                            objectName: "renameProfileButton"
                            implicitHeight: 38
                            text: qsTr("Rename")
                            enabled: !root.ctl.profileBuiltin
                            onClicked: { root.renaming = true; renameField.text = root.ctl.profileName; renameField.forceActiveFocus(); renameField.selectAll() }
                        }
                        FbButton {
                            objectName: "deleteProfileButton"
                            implicitHeight: 38
                            text: root.confirmDelete ? qsTr("Confirm delete") : qsTr("Delete")
                            enabled: !root.ctl.profileBuiltin
                            onClicked: {
                                if (root.confirmDelete) { root.ctl.deleteProfile() } else { root.confirmDelete = true }
                            }
                        }
                    }
                    Flow {
                        visible: root.renaming && !root.isMouse && !root.hotkeysTab
                        Layout.fillWidth: true
                        spacing: 10
                        FbField {
                            id: renameField
                            objectName: "renameField"
                            width: 240
                            implicitHeight: 38
                            onAccepted: { root.ctl.renameProfile(text); root.renaming = false; keyScope.forceActiveFocus() }
                            Keys.onEscapePressed: { root.renaming = false; keyScope.forceActiveFocus() }
                        }
                        FbButton {
                            objectName: "renameSaveButton"
                            kind: "primary"
                            text: qsTr("Save")
                            onClicked: { root.ctl.renameProfile(renameField.text); root.renaming = false; keyScope.forceActiveFocus() }
                        }
                        FbButton { text: qsTr("Cancel"); onClicked: { root.renaming = false; keyScope.forceActiveFocus() } }
                    }
                }
            }

            // Input test
            Rectangle {
                visible: root.width >= 1180  // narrow windows keep the mapping table, the test lives on wide ones
                Layout.fillHeight: true
                Layout.preferredWidth: Theme.inputTestWidth
                Layout.minimumWidth: Theme.inputTestWidth  // never squeezed below the 288 px grid + margins by wide text
                Layout.maximumWidth: Theme.inputTestWidth
                color: Theme.bgPanel
                Rectangle {
                    anchors.left: parent.left
                    width: 1
                    height: parent.height
                    color: Theme.borderSidebar
                }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    anchors.topMargin: Theme.space28
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Eyebrow { Layout.fillWidth: true; text: qsTr("Input test") }
                        FbLabel {
                            objectName: "testLabelsNote"
                            visible: !root.isMouse
                            text: qsTr("Labels: %1").arg(root.ctl.labelSetName)
                            color: Theme.textMeta
                            font.pixelSize: Theme.fontMeta
                        }
                    }
                    FbLabel {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: root.ctl.device.kind === "keyboard" ? qsTr("Press keys. Active inputs light up.")
                              : root.isMouse ? qsTr("The mouse drives the %1 input.").arg(root.ctl.touchLabel)
                              : qsTr("Press buttons on the controller. Active inputs light up.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    // Controller-shaped grid: 7 x 5 cells of 36, gap 6; triggers and shoulders on top, back/start in the
                    // middle, D-pad cross left, face diamond right. Glyphs follow the Button labels set.
                    Item {
                        // Fixed-width slot (panel width minus margins): whatever the text elsewhere in the column
                        // measures, the layout cannot hand the grid a wider area to center in.
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: Theme.inputTestWidth - 48
                        Layout.preferredHeight: padGrid.height
                    Item {
                        id: padGrid
                        objectName: "inputTestGrid"
                        readonly property int cell: 36
                        readonly property int gap: 6
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 7 * cell + 6 * gap
                        height: 5 * cell + 4 * gap
                        Repeater {
                            model: root.ctl.testCells
                            delegate: PadGlyph {
                                id: tile
                                required property var modelData
                                objectName: "testTile_" + modelData.id
                                x: modelData.col * (padGrid.cell + padGrid.gap)
                                y: modelData.row * (padGrid.cell + padGrid.gap)
                                width: modelData.span * padGrid.cell + (modelData.span - 1) * padGrid.gap
                                size: padGrid.cell
                                g: modelData.glyph
                                square: modelData.square
                                active: modelData.active
                            }
                        }
                    }
                    }

                    Eyebrow { text: root.ctl.touchLabel; Layout.topMargin: 10; Layout.minimumWidth: 0; Layout.maximumWidth: Theme.inputTestWidth - 48 }
                    Rectangle {
                        objectName: "touchBox"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        implicitHeight: 150
                        radius: Theme.radius8
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.borderCard
                        FbLabel {
                            anchors.centerIn: parent
                            text: qsTr("Mouse on bottom screen")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                    FbLabel {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        wrapMode: Text.WordWrap
                        text: qsTr("Left mouse button = stylus")
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontMeta
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
