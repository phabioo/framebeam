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

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space16
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
                        FbSelect {
                            objectName: "profileSelect"
                            visible: !root.isMouse && !root.hotkeysTab
                            Layout.preferredWidth: 210
                            Layout.preferredHeight: 36
                            Layout.alignment: Qt.AlignTop
                            model: root.ctl.profiles
                            current: root.ctl.profileId
                            onPicked: value => root.ctl.selectProfile(value)
                        }
                    }

                    // Tab chips + Reset N changed
                    RowLayout {
                        Layout.fillWidth: true
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
                            text: qsTr("Reset %1 changed").arg(n)
                            onClicked: root.hotkeysTab ? root.ctl.resetHotkeys() : root.ctl.resetProfile()
                        }
                    }

                    // Mouse: touch input (fixed)
                    FbLabel {
                        visible: root.isMouse && !root.hotkeysTab
                        objectName: "mouseNote"
                        Layout.fillWidth: true
                        text: qsTr("The mouse is the %1 input: move it over the touch screen, left mouse button = touch. This mapping is fixed and has no profile.").arg(root.ctl.touchLabel)
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        visible: !root.isMouse && !root.hotkeysTab && root.ctl.profileBuiltin
                        Layout.fillWidth: true
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
                        spacing: 0

                        FbLabel {
                            objectName: "hotkeysInfo"
                            Layout.fillWidth: true
                            Layout.bottomMargin: Theme.space12
                            text: qsTr("Player hotkeys work on the keyboard and are never sent to the game.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontMeta
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.bottomMargin: 6
                            spacing: Theme.space16
                            Eyebrow { Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0; text: qsTr("Action") }
                            Eyebrow { Layout.fillWidth: true; Layout.preferredWidth: root.mapWidth; Layout.minimumWidth: root.mapMinWidth; Layout.maximumWidth: root.mapWidth; text: qsTr("Key") }
                            Item { Layout.preferredWidth: root.targetWidth }
                            Item { Layout.preferredWidth: root.actionWidth }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderRow }

                        Repeater {
                            model: root.ctl.hotkeyRows
                            delegate: ColumnLayout {
                                id: hrow
                                required property var modelData
                                Layout.fillWidth: true
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
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            FbLabel { Layout.fillWidth: true; text: hrow.modelData.label; elide: Text.ElideRight }
                                            FbLabel {
                                                objectName: "hotkeyConflict_" + hrow.modelData.action
                                                visible: hrow.modelData.conflictInput !== ""
                                                Layout.fillWidth: true
                                                text: qsTr("Also mapped to %1 in the keyboard profile · the hotkey wins").arg(hrow.modelData.conflictInput)
                                                color: Theme.textMuted
                                                font.pixelSize: Theme.fontMeta
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                    Rectangle {
                                        objectName: "hotkeyField_" + hrow.modelData.action
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: root.mapWidth
                                        Layout.minimumWidth: root.mapMinWidth
                                        Layout.maximumWidth: root.mapWidth
                                        Layout.alignment: Qt.AlignTop
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
                                    Item { Layout.preferredWidth: root.targetWidth; Layout.minimumWidth: root.targetWidth; Layout.maximumWidth: root.targetWidth }
                                    RowLayout {
                                        Layout.preferredWidth: root.actionWidth
                                        Layout.minimumWidth: root.actionWidth
                                        Layout.maximumWidth: root.actionWidth
                                        Layout.alignment: Qt.AlignTop
                                        FbButton {
                                            objectName: "hotkeyReset_" + hrow.modelData.action
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
                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderRow }
                            }
                        }
                        FbLabel {
                            objectName: "hotkeyNote"
                            visible: root.ctl.hotkeyNote !== ""
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.space8
                            text: root.ctl.hotkeyNote
                            color: Theme.warn
                            font.pixelSize: Theme.fontMeta
                            wrapMode: Text.WordWrap
                        }
                    }

                    // Mapping table
                    ColumnLayout {
                        visible: !root.isMouse && !root.hotkeysTab
                        Layout.fillWidth: true
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.bottomMargin: 6
                            spacing: Theme.space16
                            Eyebrow { Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0; text: qsTr("Input") }
                            Eyebrow { Layout.fillWidth: true; Layout.preferredWidth: root.mapWidth; Layout.minimumWidth: root.mapMinWidth; Layout.maximumWidth: root.mapWidth; text: qsTr("Mapping") }
                            Eyebrow { Layout.preferredWidth: root.targetWidth; Layout.minimumWidth: root.targetWidth; Layout.maximumWidth: root.targetWidth; text: root.ctl.systemLabel }
                            Item { Layout.preferredWidth: root.actionWidth }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderRow }

                        Repeater {
                            model: root.ctl.rows
                            delegate: ColumnLayout {
                                id: mrow
                                required property var modelData
                                Layout.fillWidth: true
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
                                            objectName: "changedDot_" + mrow.modelData.input
                                            visible: mrow.modelData.changed
                                            Layout.preferredWidth: 7
                                            Layout.preferredHeight: 7
                                            radius: 3.5
                                            color: Theme.accent
                                        }
                                        FbLabel { Layout.fillWidth: true; text: mrow.modelData.label; elide: Text.ElideRight }
                                    }
                                    Rectangle {
                                        id: field
                                        objectName: "mapField_" + mrow.modelData.input
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: root.mapWidth
                                        Layout.minimumWidth: root.mapMinWidth
                                        Layout.maximumWidth: root.mapWidth
                                        implicitHeight: 30
                                        radius: Theme.radius6
                                        color: Theme.surface
                                        border.width: mrow.modelData.listening ? 1.5 : 1
                                        border.color: mrow.modelData.listening ? Theme.accent : Theme.borderInput
                                        opacity: root.ctl.profileBuiltin ? 0.85 : 1
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 4
                                            FbLabel {
                                                objectName: "mapText_" + mrow.modelData.input
                                                Layout.fillWidth: true
                                                text: mrow.modelData.listening
                                                      ? (root.ctl.device.kind === "keyboard" ? qsTr("Press a key…") : qsTr("Press a button…"))
                                                      : mrow.modelData.binding
                                                color: mrow.modelData.listening ? Theme.accent
                                                       : (mrow.modelData.mapped ? Theme.text : Theme.textFaint)
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
                                    FbMono { Layout.preferredWidth: root.targetWidth; Layout.minimumWidth: root.targetWidth; Layout.maximumWidth: root.targetWidth; text: mrow.modelData.target; font.pixelSize: Theme.fontSmall; color: Theme.textMeta }
                                    RowLayout {
                                        Layout.preferredWidth: root.actionWidth
                                        Layout.minimumWidth: root.actionWidth
                                        Layout.maximumWidth: root.actionWidth
                                        FbButton {
                                            objectName: "mapReset_" + mrow.modelData.input
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
                                spacing: 12
                                FbLabel { Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0; text: qsTr("Close lid") }
                                FbLabel { Layout.preferredWidth: root.mapWidth; Layout.minimumWidth: root.mapWidth; Layout.maximumWidth: root.mapWidth; text: qsTr("not mapped"); color: Theme.textFaint }
                                FbMono { Layout.preferredWidth: root.targetWidth; Layout.minimumWidth: root.targetWidth; Layout.maximumWidth: root.targetWidth; text: "LID"; font.pixelSize: Theme.fontSmall; color: Theme.textMeta }
                                Item { Layout.preferredWidth: root.actionWidth }
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

                    Eyebrow { text: qsTr("Input test") }
                    FbLabel {
                        Layout.fillWidth: true
                        text: root.ctl.device.kind === "keyboard" ? qsTr("Press keys — active inputs light up.")
                              : root.isMouse ? qsTr("The mouse drives the %1 input.").arg(root.ctl.touchLabel)
                              : qsTr("Press buttons on the controller — active inputs light up.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    GridLayout {
                        columns: 4
                        rowSpacing: 8
                        columnSpacing: 8
                        Layout.fillWidth: true
                        Repeater {
                            model: [
                                { id: "a", text: "A" }, { id: "b", text: "B" }, { id: "x", text: "X" }, { id: "y", text: "Y" },
                                { id: "l", text: "L" }, { id: "r", text: "R" }, { id: "up", text: "▲" }, { id: "down", text: "▼" },
                                { id: "left", text: "◀" }, { id: "right", text: "▶" }, { id: "start", text: "ST" }, { id: "select", text: "SE" }
                            ]
                            delegate: Rectangle {
                                id: tile
                                required property var modelData
                                objectName: "testTile_" + modelData.id
                                readonly property bool active: root.ctl.activeInputs.indexOf(modelData.id) >= 0
                                Layout.fillWidth: true
                                implicitHeight: 44
                                radius: Theme.radius8
                                color: tile.active ? Theme.accent : Theme.surface
                                border.width: 1
                                border.color: tile.active ? Theme.accent : Theme.borderCard
                                Behavior on color { ColorAnimation { duration: 60 } }
                                Text {
                                    anchors.centerIn: parent
                                    text: tile.modelData.text
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.Medium
                                    color: tile.active ? Theme.textOnAccent : Theme.textSecondary
                                }
                            }
                        }
                    }

                    Eyebrow { text: root.ctl.touchLabel; Layout.topMargin: 10 }
                    Rectangle {
                        objectName: "touchBox"
                        Layout.fillWidth: true
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
