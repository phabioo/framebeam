import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Uzume.App

// "Report a problem" (design 6d, 6d-2): description, linked event, the attachments with check boxes, and the footer states
// sending / sent / Hub offline / failed. Dialog 580. Ctrl+Enter sends, Esc cancels (and aborts a running upload).
// The state lives in PlayerController.report (ReportController); this file only draws it.
Popup {
    id: root
    objectName: "reportDialog"
    required property PlayerController player
    readonly property ReportController ctl: root.player.report
    readonly property string st: root.ctl.state
    readonly property bool locked: root.st === "sending" || root.st === "sent" || root.st === "offline"
    property bool showFiles: false

    anchors.centerIn: parent
    modal: true
    dim: true
    focus: true
    width: Math.min(580, parent ? parent.width - 32 : 580)
    height: Math.min(implicitHeight, parent ? parent.height - 32 : implicitHeight)
    padding: 0
    closePolicy: Popup.CloseOnEscape   // a typed text is never lost to a stray click

    Component.onCompleted: if (root.ctl.open) root.open()
    Connections {
        target: root.ctl
        function onChanged() {
            if (root.ctl.open && !root.opened) root.open()
            else if (!root.ctl.open && root.opened) root.close()
        }
    }
    onOpened: { root.showFiles = false; description.forceActiveFocus() }
    onClosed: if (root.ctl.open) root.ctl.close()

    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: root.opened
        onActivated: if (root.ctl.canSend) root.ctl.send()
    }

    background: Rectangle {
        radius: Theme.radius12
        color: Theme.popupBg
        border.width: 1
        border.color: Theme.borderPopup
    }

    contentItem: ColumnLayout {
        objectName: "reportCard"
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            Layout.topMargin: 24
            Layout.bottomMargin: 12
            spacing: 4
            UzLabel {
                Layout.fillWidth: true
                text: qsTr("Report a problem")
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            UzLabel {
                objectName: "reportSub"
                Layout.fillWidth: true
                text: qsTr("Goes to the admins of %1. They see it under Reports.").arg(root.ctl.hubName)
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }

        // The body scrolls when the window is too small for it (long error text, wide fonts); header and footer stay put.
        Flickable {
            id: body
            objectName: "reportBody"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            Layout.preferredHeight: bodyCol.implicitHeight
            contentWidth: width
            contentHeight: bodyCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }

            ColumnLayout {
                id: bodyCol
                width: body.width
                spacing: 0

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 28
                    Layout.rightMargin: 28
                    spacing: 6
                    UzLabel { text: qsTr("What happened?"); font.pixelSize: Theme.fontSmall; font.weight: Font.Medium }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
                        clip: true
                        background: Rectangle {
                            radius: Theme.radius8
                            color: Theme.surface
                            border.width: description.activeFocus ? 1.5 : 1
                            border.color: description.activeFocus ? Theme.accent : Theme.borderInput
                        }
                        TextArea {
                            id: description
                            objectName: "reportDescription"
                            text: root.ctl.description
                            readOnly: root.locked
                            wrapMode: TextEdit.Wrap
                            selectByMouse: true
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            selectionColor: Theme.accent
                            selectedTextColor: Theme.textOnAccent
                            padding: 10
                            leftPadding: 12
                            rightPadding: 12
                            background: null
                            Accessible.name: qsTr("What happened?")
                            onTextChanged: {
                                if (text.length > root.ctl.maxDescription) text = text.substring(0, root.ctl.maxDescription)
                                if (text !== root.ctl.description) root.ctl.description = text
                            }
                        }
                    }
                    UzLabel {
                        objectName: "reportLinked"
                        visible: root.ctl.linkedText !== ""
                        text: root.ctl.linkedText
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontMeta
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 28
                    Layout.rightMargin: 28
                    Layout.topMargin: 16
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Eyebrow { objectName: "reportTotal"; text: qsTr("Attached · %1").arg(root.ctl.totalText) }
                        Item { Layout.fillWidth: true }
                        UzButton {
                            objectName: "reportSeeIncluded"
                            kind: "link"
                            text: root.showFiles ? qsTr("Hide what is included") : qsTr("See what is included")
                            onClicked: root.showFiles = !root.showFiles
                        }
                    }
                    UzLabel {
                        objectName: "reportPreparing"
                        visible: root.st === "preparing"
                        text: qsTr("Collecting logs…")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                    }
                    Repeater {
                        model: root.ctl.attachments
                        delegate: Rectangle {
                            id: row
                            required property var modelData
                            objectName: "reportAttach_" + row.modelData.id
                            Layout.fillWidth: true
                            implicitHeight: 34
                            color: "transparent"
                            Accessible.role: Accessible.CheckBox
                            Accessible.name: row.modelData.label
                            Accessible.checked: row.modelData.checked
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderRow }
                            RowLayout {
                                anchors.fill: parent
                                spacing: 10
                                Rectangle {
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    radius: 0
                                    color: row.modelData.checked ? Theme.accent : "transparent"
                                    border.width: 1.5
                                    border.color: row.modelData.checked ? Theme.accent : Theme.borderButton
                                    opacity: root.locked ? 0.5 : 1
                                    Text {
                                        anchors.centerIn: parent
                                        visible: row.modelData.checked
                                        text: "✓"
                                        font.pixelSize: 11
                                        font.weight: Font.Bold
                                        color: Theme.textOnAccent
                                    }
                                }
                                UzLabel {
                                    Layout.fillWidth: true
                                    text: row.modelData.label + (row.modelData.detail !== "" ? "  ·  " + row.modelData.detail : "")
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.fontSmall
                                }
                                UzMono { text: row.modelData.sizeText; color: Theme.textFaint; font.pixelSize: Theme.fontMeta }
                            }
                            TapHandler {
                                enabled: !root.locked
                                onTapped: root.ctl.setChecked(row.modelData.id, !row.modelData.checked)
                            }
                            HoverHandler { cursorShape: root.locked ? Qt.ArrowCursor : Qt.PointingHandCursor }
                        }
                    }
                    UzLabel {
                        objectName: "reportFiles"
                        visible: root.showFiles
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        text: qsTr("Sent: your text") + (root.ctl.includedFiles.length > 0 ? ", " + root.ctl.includedFiles.join(", ") : "")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontMeta
                        font.family: Theme.mono
                        wrapMode: Text.WrapAnywhere
                    }
                    UzLabel {
                        objectName: "reportBlocked"
                        visible: root.ctl.blockedText !== ""
                        Layout.fillWidth: true
                        text: root.ctl.blockedText
                        color: Theme.pluginWarn
                        font.pixelSize: Theme.fontMeta
                        wrapMode: Text.WordWrap
                    }
                    UzLabel {
                        objectName: "reportNever"
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        text: qsTr("Never included: passwords, the Hub pairing token, ROMs, saves.")
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontMeta
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // Footer: the buttons while editing, the state card while sending / sent / offline / failed.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            Layout.topMargin: 16
            Layout.bottomMargin: 20
            spacing: 12

            Rectangle {
                id: stateCard
                objectName: "reportState"
                visible: root.st === "sending" || root.st === "sent" || root.st === "offline" || root.st === "failed"
                Layout.fillWidth: true
                implicitHeight: stateCol.implicitHeight + 24
                radius: Theme.radius9
                color: root.st === "failed" ? Theme.errorBg : Theme.surfaceRaised
                border.width: 1
                border.color: root.st === "failed" ? Theme.dangerBorder : Theme.borderCard
                ColumnLayout {
                    id: stateCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6
                    RowLayout {
                        spacing: 8
                        UzLabel {
                            text: root.st === "sent" ? "✓" : root.st === "offline" ? "⊘" : root.st === "failed" ? "▲" : ""
                            visible: text !== ""
                            color: root.st === "sent" ? Theme.ok : root.st === "failed" ? Theme.error : Theme.textMuted
                        }
                        UzLabel {
                            objectName: "reportStateTitle"
                            font.weight: Font.DemiBold
                            text: root.st === "sending" ? qsTr("Sending to %1…").arg(root.ctl.hubName)
                                : root.st === "sent" ? qsTr("Report sent · #%1").arg(root.ctl.reportId)
                                : root.st === "offline" ? qsTr("%1 is offline").arg(root.ctl.hubName)
                                : qsTr("Could not send")
                        }
                    }
                    UzLabel {
                        objectName: "reportStateText"
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                        text: root.st === "sending" ? qsTr("%1. You can keep using the Player.").arg(root.ctl.progressText)
                            : root.st === "sent" ? qsTr("The admins of %1 see it under Reports. Nothing else to do.").arg(root.ctl.hubName)
                            : root.st === "offline" ? qsTr("Save the report as a file and send it later, or keep the dialog open; it sends when the Hub is back.")
                            : root.ctl.errorText
                    }
                    Rectangle {
                        objectName: "reportProgress"
                        visible: root.st === "sending"
                        Layout.fillWidth: true
                        implicitHeight: 4
                        radius: 0
                        color: Theme.borderCard
                        Rectangle {
                            width: parent.width * root.ctl.progress
                            height: parent.height
                            radius: 0
                            color: Theme.accent
                        }
                    }
                }
            }

            UzLabel {
                objectName: "reportSaveNote"
                visible: root.ctl.saveNote !== ""
                Layout.fillWidth: true
                text: root.ctl.saveNote
                color: Theme.textMuted
                font.pixelSize: Theme.fontMeta
                wrapMode: Text.WrapAnywhere
            }

            // Right to left: Send, Cancel / Done, then Save as file. With wide fonts the row wraps instead of leaving the dialog.
            Flow {
                objectName: "reportActions"
                Layout.fillWidth: true
                spacing: Theme.space8
                layoutDirection: Qt.RightToLeft
                UzButton {
                    objectName: "reportSend"
                    visible: root.st !== "sent" && root.st !== "sending"
                    kind: "primary"
                    enabled: root.ctl.canSend
                    text: root.st === "failed" ? qsTr("Try again") : qsTr("Send to %1").arg(root.ctl.hubName)
                    onClicked: root.ctl.send()
                }
                UzButton {
                    objectName: "reportDone"
                    visible: root.st === "sent"
                    kind: "primary"
                    text: qsTr("Done")
                    onClicked: root.close()
                }
                UzButton {
                    objectName: "reportCancel"
                    visible: root.st !== "sent"
                    text: qsTr("Cancel")
                    onClicked: root.st === "sending" ? root.ctl.cancelSending() : root.close()
                }
                UzButton {
                    objectName: "reportSaveAsFile"
                    visible: root.st !== "sent" && root.st !== "sending"
                    enabled: root.st !== "preparing"
                    text: qsTr("Save as file…")
                    onClicked: saveDialog.open()
                }
            }
        }
    }

    // Native save dialog of "Save as file…"; loaded on first use so the QtQuick.Dialogs module is only needed then.
    Loader {
        id: saveDialog
        active: false
        source: "ReportSaveDialog.qml"
        function open() {
            if (item) {
                item.prepare()
                item.open()
            } else {
                active = true
            }
        }
        onLoaded: {
            item.ctl = root.ctl
            item.prepare()
            item.open()
        }
    }
}
