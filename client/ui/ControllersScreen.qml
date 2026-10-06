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
                    spacing: 6

                    Eyebrow { text: qsTr("Devices"); Layout.leftMargin: 4; Layout.bottomMargin: 4 }

                    Repeater {
                        model: root.ctl.devices
                        delegate: Rectangle {
                            id: dev
                            required property var modelData
                            objectName: "device_" + modelData.key
                            readonly property bool selected: modelData.key === root.ctl.selectedDevice
                            Layout.fillWidth: true
                            implicitHeight: devCol.implicitHeight + 24
                            radius: 8
                            color: dev.selected ? Theme.surfaceRaised : "transparent"
                            ColumnLayout {
                                id: devCol
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 3
                                RowLayout {
                                    FbLabel {
                                        Layout.fillWidth: true
                                        text: dev.modelData.name
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }
                                    FbMono {
                                        objectName: "deviceSlot_" + dev.modelData.key
                                        text: dev.modelData.slot
                                        font.pixelSize: 11
                                    }
                                }
                                FbLabel {
                                    text: dev.modelData.profile
                                    color: Theme.textMuted
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                            TapHandler { onTapped: root.ctl.selectDevice(dev.modelData.key) }
                        }
                    }

                    FbLabel {
                        visible: !root.ctl.gamepadAvailable
                        objectName: "gamepadNote"
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        text: root.ctl.gamepadNote
                        color: Theme.warn
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    FbLabel {
                        visible: root.ctl.gamepadAvailable && root.ctl.devices.length <= 2
                        objectName: "noGamepadHint"
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        text: qsTr("No gamepad connected. Connect one and it appears here (the first gamepad is P1).")
                        color: Theme.textFaint
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    Item { Layout.fillHeight: true }
                    FbLabel {
                        objectName: "profilesLocalFooter"
                        Layout.fillWidth: true
                        text: qsTr("Profiles stay local on this device and are not synchronized.")
                        color: Theme.textFaint
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
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

                    FbLabel {
                        objectName: "controllersTitle"
                        Layout.fillWidth: true
                        text: root.ctl.device.name !== undefined ? root.ctl.device.name : qsTr("Controllers")
                        font.pixelSize: 26
                        font.weight: Font.DemiBold
                        font.letterSpacing: -0.4
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        visible: !root.isMouse
                        Layout.fillWidth: true
                        Layout.topMargin: -8
                        spacing: 12
                        FbLabel {
                            text: qsTr("Profile:")
                            color: Theme.textMuted
                            font.pixelSize: 13
                        }
                        FbSelect {
                            objectName: "profileSelect"
                            Layout.preferredWidth: 260
                            model: root.ctl.profiles
                            current: root.ctl.profileId
                            onPicked: value => root.ctl.selectProfile(value)
                        }
                        Item { Layout.fillWidth: true }
                    }

                    // Mouse: DS touch (fixed)
                    FbLabel {
                        visible: root.isMouse
                        objectName: "mouseNote"
                        Layout.fillWidth: true
                        text: qsTr("The mouse is the DS touch screen: move it over the lower screen, left mouse button = stylus. This mapping is fixed and has no profile.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                    }

                    FbLabel {
                        visible: !root.isMouse && root.ctl.profileBuiltin
                        objectName: "builtinNote"
                        Layout.fillWidth: true
                        text: qsTr("Built-in profiles are read-only. Duplicate the profile to change the mapping.")
                        color: Theme.textMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    // Mapping table
                    ColumnLayout {
                        visible: !root.isMouse
                        Layout.fillWidth: true
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.bottomMargin: 6
                            spacing: 12
                            Eyebrow { Layout.fillWidth: true; text: qsTr("FrameBeam input") }
                            Eyebrow { Layout.preferredWidth: 190; text: qsTr("Mapping") }
                            Eyebrow { Layout.preferredWidth: 90; text: qsTr("NDS") }
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
                                    spacing: 12
                                    FbLabel { Layout.fillWidth: true; text: mrow.modelData.label }
                                    Rectangle {
                                        id: field
                                        objectName: "mapField_" + mrow.modelData.input
                                        Layout.preferredWidth: 190
                                        implicitHeight: 30
                                        radius: 6
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
                                                font.pixelSize: 13
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
                                            enabled: !root.ctl.profileBuiltin
                                            onTapped: mrow.modelData.listening ? root.ctl.cancelCapture() : root.ctl.beginCapture(mrow.modelData.input)
                                        }
                                    }
                                    FbMono { Layout.preferredWidth: 90; text: mrow.modelData.target; font.pixelSize: 12; color: Theme.textSecondary }
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
                                FbLabel { Layout.fillWidth: true; text: qsTr("Close lid") }
                                FbLabel { Layout.preferredWidth: 190; text: qsTr("not mapped"); color: Theme.textFaint }
                                FbMono { Layout.preferredWidth: 90; text: "LID"; font.pixelSize: 12 }
                            }
                        }
                    }

                    // Profile actions
                    Flow {
                        visible: !root.isMouse
                        Layout.fillWidth: true
                        spacing: 10
                        FbButton {
                            objectName: "resetProfileButton"
                            text: qsTr("Reset to default")
                            enabled: !root.ctl.profileBuiltin
                            onClicked: root.ctl.resetProfile()
                        }
                        FbButton {
                            objectName: "duplicateProfileButton"
                            text: qsTr("Duplicate profile")
                            onClicked: root.ctl.duplicateProfile()
                        }
                        FbButton {
                            objectName: "renameProfileButton"
                            text: qsTr("Rename")
                            enabled: !root.ctl.profileBuiltin
                            onClicked: { root.renaming = true; renameField.text = root.ctl.profileName; renameField.forceActiveFocus(); renameField.selectAll() }
                        }
                        FbButton {
                            objectName: "deleteProfileButton"
                            text: root.confirmDelete ? qsTr("Confirm delete") : qsTr("Delete")
                            enabled: !root.ctl.profileBuiltin
                            onClicked: {
                                if (root.confirmDelete) { root.ctl.deleteProfile() } else { root.confirmDelete = true }
                            }
                        }
                    }
                    Flow {
                        visible: root.renaming && !root.isMouse
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
                visible: root.width >= 1100  // narrow windows keep the mapping table, the test lives on wide ones
                Layout.fillHeight: true
                Layout.preferredWidth: 300
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
                    spacing: 14

                    Eyebrow { text: qsTr("Input test") }
                    FbLabel {
                        Layout.fillWidth: true
                        text: root.ctl.device.kind === "keyboard" ? qsTr("Press keys — active inputs light up.")
                              : root.isMouse ? qsTr("The mouse drives the DS touch screen.")
                              : qsTr("Press buttons on the controller — active inputs light up.")
                        color: Theme.textMuted
                        font.pixelSize: 13
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
                                radius: 8
                                color: tile.active ? Theme.accent : Theme.surfaceRaised
                                border.width: 1
                                border.color: tile.active ? Theme.accent : Theme.borderCard
                                Text {
                                    anchors.centerIn: parent
                                    text: tile.modelData.text
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                    color: tile.active ? Theme.textOnAccent : Theme.textSecondary
                                }
                            }
                        }
                    }

                    Eyebrow { text: qsTr("DS touch"); Layout.topMargin: 10 }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 150
                        radius: 8
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.borderCard
                        FbLabel {
                            anchors.centerIn: parent
                            text: qsTr("Mouse on lower screen")
                            color: Theme.textMuted
                            font.pixelSize: 13
                        }
                    }
                    FbLabel {
                        text: qsTr("Left mouse button = stylus")
                        color: Theme.textFaint
                        font.pixelSize: 12
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
