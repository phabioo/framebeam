import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// Sort select of the Library toolbar (3c-2): 32 high, "Sort: <value> ▾". The menu is 252 wide with grouped items,
// a "✓" on the selected one and a footer note. picked(key) on user choice; the owner stores it.
Rectangle {
    id: root
    property string current: "name_asc"
    signal picked(string key)

    readonly property var sections: [
        { header: qsTr("Name"), items: [
            { value: "name_asc", label: qsTr("A–Z"), short: qsTr("Name A–Z") },
            { value: "name_desc", label: qsTr("Z–A"), short: qsTr("Name Z–A") } ] },
        { header: qsTr("Date added to Hub"), items: [
            { value: "added_desc", label: qsTr("Newest first"), short: qsTr("Date added, newest") },
            { value: "added_asc", label: qsTr("Oldest first"), short: qsTr("Date added, oldest") } ] },
        { header: qsTr("More"), items: [
            { value: "size_desc", label: qsTr("Size largest first"), short: qsTr("Size, largest") },
            { value: "size_asc", label: qsTr("Size smallest first"), short: qsTr("Size, smallest") },
            { value: "system", label: qsTr("System then name"), short: qsTr("System") },
            { value: "played", label: qsTr("Last played this device"), short: qsTr("Last played") } ] }
    ]
    readonly property string currentLabel: {
        for (const sec of sections) {
            for (const it of sec.items) {
                if (it.value === current) return it.short
            }
        }
        return sections[0].items[0].short
    }
    readonly property alias menuOpen: menu.visible

    objectName: "sortControl"
    implicitHeight: 32
    implicitWidth: label.implicitWidth + 36 + 14
    Layout.minimumWidth: 0
    radius: Theme.radius7
    color: Theme.surface
    border.width: activeFocus || menu.visible ? 1.5 : 1
    border.color: activeFocus || menu.visible ? Theme.accent : (hover.hovered ? Theme.borderButton : Theme.borderInput)
    activeFocusOnTab: true
    Accessible.role: Accessible.ComboBox
    Accessible.name: qsTr("Sort: %1").arg(currentLabel)

    Row {
        id: row
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 10
        spacing: 6
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, Math.max(0, root.width - 12 - 10 - arrow.implicitWidth - parent.spacing))
            elide: Text.ElideRight
            textFormat: Text.StyledText
            text: "<font color='" + Theme.textMeta + "'>" + qsTr("Sort:") + "</font> " + root.currentLabel
            font.pixelSize: Theme.fontSmall
            color: Theme.text
        }
        Text {
            id: arrow
            anchors.verticalCenter: parent.verticalCenter
            text: menu.visible ? "▴" : "▾"
            font.pixelSize: Theme.fontMono
            color: Theme.textFaint
        }
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: { root.forceActiveFocus(); menu.visible ? menu.close() : menu.open() } }
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space || event.key === Qt.Key_Down) {
            menu.open()
            event.accepted = true
        }
    }

    Popup {
        id: menu
        objectName: "sortMenu"
        parent: root
        x: root.width - width
        y: root.height + 4
        width: 252
        padding: 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: Theme.radius8
            color: Theme.popupBg
            border.width: 1
            border.color: Theme.borderPopup
        }
        contentItem: ColumnLayout {
            spacing: 0
            Repeater {
                model: root.sections
                delegate: ColumnLayout {
                    id: sec
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 0
                    Eyebrow {
                        Layout.fillWidth: true
                        Layout.topMargin: sec.index === 0 ? 4 : 8
                        Layout.leftMargin: 8
                        Layout.bottomMargin: 2
                        text: sec.modelData.header
                    }
                    Repeater {
                        model: sec.modelData.items
                        delegate: ItemDelegate {
                            id: item
                            required property var modelData
                            objectName: "sort_" + modelData.value
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            implicitHeight: 30
                            padding: 0
                            leftPadding: 8
                            rightPadding: 8
                            onClicked: { const key = item.modelData.value; menu.close(); root.picked(key) }
                            contentItem: Row {
                                spacing: 4
                                Text {
                                    width: 16
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: item.modelData.value === root.current ? "✓" : ""
                                    font.pixelSize: Theme.fontSmall
                                    color: Theme.accent
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: item.width - 16 - 4 - 16
                                    text: item.modelData.label
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.fontSmall
                                    color: Theme.text
                                }
                            }
                            background: Rectangle {
                                radius: Theme.radius6
                                color: item.highlighted || item.hovered ? Theme.surfaceRaised : "transparent"
                            }
                        }
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.topMargin: 6; height: 1; color: Theme.borderSidebar }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                Layout.bottomMargin: 4
                text: qsTr("Never played games come last. Sort and Ready first are saved on this device.")
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontMono
                color: Theme.textFaint
            }
        }
    }
}
