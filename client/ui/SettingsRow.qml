import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameBeam.Player

// SettingsRow (player.md "Components (revision v4) -> SettingsRow"): the one row of the Emulation (3e-2) and Settings
// (3p-2) pages. Grid 14 | flex | 280 | 72: the changed-dot slot (the label never shifts), the text column, the control column
// (right edge = the same x on every page) and the reset column (stays empty with resetMode "none").
// Core-agnostic: the row only knows label, description, values and flags; the control kind follows from the values
// (boolean pair -> toggle, at most 4 labels that fit their cell -> segment, otherwise select).
// Below Theme.settingsNarrowBelow the control column moves under the text so nothing widens past the page column.
Item {
    id: root

    // ---- content
    property string label: ""
    property string description: ""        // raw core text; "Option: explanation" lines become the option list
    property string meta: ""               // inline mono meta after the label ("Channel: beta")
    property string badge: ""              // e.g. "applies on next start"
    property string disabledReason: ""     // non-empty: the control is disabled and the reason is shown
    property string idKey: ""              // option key; derives the objectNames of dot, Reset, Default and badge
    property string controlName: ""        // objectName of the control (select, segment, value)
    property string toggleName: ""         // objectName of a toggle (defaults to controlName)
    property string descriptionName: ""    // objectName of the description text

    // ---- control
    property string ctrl: "auto"           // auto | toggle | segment | select | buttons | value
    property var values: []                // [{ value, label, name? }]
    property string current: ""            // selected value
    property bool checked: false           // toggle without a value list
    property var buttons: []               // [{ name, text, enabled?, visible?, busy?, busyOnClick? }]
    property string valueText: ""          // read-only value
    property bool controlEnabled: true

    // ---- state
    property bool changed: false
    property string resetMode: "reset"     // reset | none
    property bool expanded: false

    signal valuePicked(string value)
    signal toggled(bool checked)
    signal buttonClicked(string name)
    signal resetRequested()

    // ---- geometry (read by tests)
    readonly property bool narrow: width < Theme.settingsNarrowBelow
    readonly property real controlWidth: narrow ? Math.max(120, Math.min(Theme.settingsControlWidth, width - Theme.settingsDotSlot - Theme.settingsResetWidth))
                                               : Theme.settingsControlWidth
    readonly property real textWidth: Math.max(0, narrow ? width - Theme.settingsDotSlot
                                                         : width - Theme.settingsDotSlot - Theme.settingsControlWidth - Theme.settingsResetWidth - 32)
    readonly property alias controlColumn: ctlColumn
    readonly property alias resetColumn: resetCol
    readonly property alias labelItem: labelText
    readonly property alias textColumn: textCol
    readonly property alias descriptionItem: introText
    readonly property alias dotItem: dot
    readonly property alias controlLoader: ctlLoader

    // ---- resolved control kind
    readonly property bool disabled: root.disabledReason !== ""
    readonly property bool isBoolean: {
        if (root.values.length !== 2) {
            return false
        }
        const v = [root.values[0].value, root.values[1].value].join("|")
        return ["off|on", "disabled|enabled", "false|true", "0|1"].indexOf(v) >= 0
    }
    // Segment only when every label fits its cell (control width / n - 12px of padding), else select. Measured in
    // characters (6.5 px each), not font metrics, so the kind never flips with the platform font.
    readonly property bool segmentFits: {
        const n = root.values.length
        if (n < 2 || n > 4) {
            return false
        }
        const cell = root.controlWidth / n - 12
        for (let i = 0; i < n; ++i) {
            if (String(root.values[i].label || "").length > Math.floor(cell / 6.5)) {
                return false
            }
        }
        return true
    }
    readonly property string kind: {
        if (root.ctrl === "buttons" || root.ctrl === "value" || root.ctrl === "toggle" || root.ctrl === "select") {
            return root.ctrl
        }
        if (root.ctrl === "auto" && root.isBoolean) {
            return "toggle"
        }
        return root.segmentFits ? "segment" : "select"
    }
    readonly property bool toggleChecked: root.values.length === 2 ? root.current === root.values[1].value : root.checked

    // ---- description: intro + option list
    function parseDescription(text: string): var {
        const intro = []
        const list = []
        let seenFirst = false
        const lines = String(text).split(/\r?\n/)
        for (let i = 0; i < lines.length; ++i) {
            const ln = lines[i].trim()
            if (ln === "") {
                continue
            }
            const dash = /^[-•*]\s+/.test(ln)
            const m = /^([^:]{1,48}):\s+(.+)$/.exec(ln.replace(/^[-•*]\s+/, ""))
            if (m !== null && (dash || seenFirst)) {
                list.push({ k: m[1].trim(), v: m[2].trim() })
            } else {
                intro.push(ln)
            }
            seenFirst = true
        }
        return { intro: intro.join(" "), list: list }
    }
    readonly property var parsed: root.parseDescription(root.description)
    readonly property string intro: root.parsed.intro
    readonly property var optionList: root.parsed.list
    readonly property bool hasList: root.optionList.length > 0
    readonly property bool overflows: descProbe.lineCount > 2
    readonly property bool canExpand: root.hasList || root.overflows
    readonly property bool showFull: root.expanded && root.canExpand
    readonly property string moreLabel: root.showFull ? qsTr("Less")
                                        : (root.hasList ? qsTr("More · %1").arg(root.optionList.length === 1 ? qsTr("1 option") : qsTr("%1 options").arg(root.optionList.length))
                                                        : qsTr("More"))

    // Buttons wrap onto further lines when the control column is too narrow for them (wide fonts), so the control may be taller.
    property real buttonsHeight: 0   // set by the buttons control (not bound: the Loader item would loop)
    readonly property real ctlHeight: root.kind === "buttons" ? Math.max(Theme.settingsControlHeight, root.buttonsHeight) : Theme.settingsControlHeight
    implicitHeight: (root.narrow ? Theme.settingsDotSlot + textCol.implicitHeight + 8 + root.ctlHeight
                                 : Theme.settingsDotSlot + Math.max(root.ctlHeight, textCol.implicitHeight))
                    + Theme.settingsDotSlot + 1


    // Hidden measurement of the full intro at the clamp width: more than 2 lines -> "More".
    Text {
        id: descProbe
        visible: false
        width: Math.min(560, root.textWidth)
        text: root.intro
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontMeta
        lineHeight: 18
        lineHeightMode: Text.FixedHeight
    }

    // Row surface (hover) and divider
    Rectangle {
        anchors.fill: parent
        color: hover.hovered ? Theme.rowHover : "transparent"
    }
    HoverHandler { id: hover }
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.borderRow }

    // Dot slot (14): the label never shifts, the dot is centered on the first (32 high) label line.
    Item {
        x: 0
        y: Theme.settingsDotSlot
        width: Theme.settingsDotSlot
        height: Theme.settingsControlHeight
        Rectangle {
            id: dot
            objectName: root.idKey !== "" ? "changedDot_" + root.idKey : ""
            visible: root.changed
            anchors.centerIn: parent
            width: 6
            height: 6
            radius: 3
            color: Theme.accent
        }
    }

    // Text column
    ColumnLayout {
        id: textCol
        x: Theme.settingsDotSlot
        y: Theme.settingsDotSlot
        width: root.textWidth
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.minimumHeight: Theme.settingsControlHeight
            spacing: Theme.space8
            FbLabel {
                id: labelText
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.label
                font.weight: Font.Medium
                wrapMode: Text.Wrap
                lineHeight: 20
                lineHeightMode: Text.FixedHeight
                verticalAlignment: Text.AlignVCenter
                color: root.disabled ? Theme.textMeta : Theme.text
            }
            // The wrapper takes its width from an unelided probe (an elided Text reports a clamped implicit width inside a layout).
            Item {
                visible: root.meta !== ""
                Layout.preferredWidth: Math.ceil(metaProbe.implicitWidth) + 1
                Layout.minimumWidth: 0
                implicitWidth: metaProbe.implicitWidth
                clip: true
                implicitHeight: metaProbe.implicitHeight
                Text {
                    id: metaProbe
                    visible: false
                    text: root.meta
                    font.family: Theme.mono
                    font.pixelSize: Theme.fontMono
                }
                Text {
                    id: metaVis
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.meta
                    font.family: Theme.mono
                    font.pixelSize: Theme.fontMono
                    color: Theme.textFaint
                }
            }
            Rectangle {
                objectName: root.idKey !== "" ? "restartBadge_" + root.idKey : ""
                visible: root.badge !== ""
                Layout.preferredWidth: Math.ceil(badgeProbe.implicitWidth) + 13
                Layout.minimumWidth: 0
                implicitWidth: badgeProbe.implicitWidth + 12
                implicitHeight: 18
                radius: Theme.radius4
                color: Theme.neutralPillBg
                Text {
                    id: badgeProbe
                    visible: false
                    text: root.badge
                    font.pixelSize: Theme.fontMono
                }
                clip: true
                Text {
                    id: badgeText
                    x: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.badge
                    font.pixelSize: Theme.fontMono
                    color: Theme.textMuted
                }
            }
        }

        // Description: clamped to 2 lines with "More"; expanded: full intro and the two-column option list.
        Text {
            id: introText
            objectName: root.descriptionName
            visible: root.intro !== ""
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: 560
            text: root.intro
            wrapMode: Text.Wrap
            maximumLineCount: root.showFull ? 100000 : 2
            elide: root.showFull ? Text.ElideNone : Text.ElideRight
            font.pixelSize: Theme.fontMeta
            lineHeight: 18
            lineHeightMode: Text.FixedHeight
            color: Theme.textMeta
        }
        Rectangle {
            id: optionBox
            objectName: root.idKey !== "" ? "optionList_" + root.idKey : ""
            visible: root.showFull && root.hasList
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: 560
            Layout.topMargin: 6
            implicitHeight: optionGrid.implicitHeight + 16
            radius: Theme.radius6
            color: Theme.bgPanel
            GridLayout {
                id: optionGrid
                x: 12
                y: 8
                width: parent.width - 24
                columns: 2
                columnSpacing: 12
                rowSpacing: 4
                Repeater {
                    model: root.optionList.length * 2
                    delegate: Text {
                        required property int index
                        readonly property var entry: root.optionList[Math.floor(index / 2)]
                        readonly property bool isKey: index % 2 === 0
                        Layout.fillWidth: !isKey
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: isKey ? 160 : Number.POSITIVE_INFINITY
                        Layout.alignment: Qt.AlignTop
                        text: isKey ? entry.k : entry.v
                        wrapMode: isKey ? Text.NoWrap : Text.Wrap
                        elide: isKey ? Text.ElideRight : Text.ElideNone
                        font.pixelSize: Theme.fontMeta
                        font.weight: isKey ? Font.Medium : Font.Normal
                        lineHeight: 18
                        lineHeightMode: Text.FixedHeight
                        color: isKey ? Theme.textSecondary : Theme.textMeta
                    }
                }
            }
        }
        Item {
            id: moreLink
            objectName: root.idKey !== "" ? "descMore_" + root.idKey : ""
            visible: root.intro !== "" && (root.canExpand || root.expanded)
            Layout.preferredWidth: moreText.implicitWidth
            Layout.preferredHeight: 18
            Layout.topMargin: 2
            activeFocusOnTab: true
            Keys.onReturnPressed: root.expanded = !root.expanded
            Keys.onEnterPressed: root.expanded = !root.expanded
            Keys.onSpacePressed: root.expanded = !root.expanded
            Text {
                id: moreText
                text: root.moreLabel
                font.pixelSize: Theme.fontMeta
                font.underline: true
                color: moreLink.activeFocus ? Theme.accent : Theme.textMuted
            }
            TapHandler { onTapped: root.expanded = !root.expanded }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
        }
        Text {
            objectName: root.idKey !== "" ? "disabledReason_" + root.idKey : ""
            visible: root.disabled
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.topMargin: 2
            text: "⊘ " + root.disabledReason
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontMeta
            lineHeight: 18
            lineHeightMode: Text.FixedHeight
            color: Theme.textSecondary
        }
    }

    // Control column (right edge = row width - 72 on wide rows)
    Item {
        id: ctlColumn
        objectName: root.idKey !== "" ? "rowControl_" + root.idKey : ""
        x: root.narrow ? Theme.settingsDotSlot : root.width - Theme.settingsResetWidth - Theme.settingsControlWidth
        y: root.narrow ? Theme.settingsDotSlot + textCol.implicitHeight + 8 : Theme.settingsDotSlot
        width: root.controlWidth
        height: root.ctlHeight
        Loader {
            id: ctlLoader
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            enabled: root.controlEnabled && !root.disabled
            opacity: root.disabled ? 0.4 : 1
            sourceComponent: root.kind === "toggle" ? toggleComp
                           : root.kind === "segment" ? segmentComp
                           : root.kind === "select" ? selectComp
                           : root.kind === "buttons" ? buttonsComp
                           : valueComp
        }
    }

    // Reset column (72): "Reset" when changed, "Default" otherwise, empty with resetMode "none".
    Item {
        id: resetCol
        x: root.width - Theme.settingsResetWidth
        y: ctlColumn.y
        width: Theme.settingsResetWidth
        height: Theme.settingsControlHeight
        visible: root.resetMode !== "none"
        Text {
            objectName: root.idKey !== "" ? "optionOrigin_" + root.idKey : ""
            visible: !root.changed
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Default")
            font.pixelSize: Theme.fontMeta
            color: Theme.textDisabled
        }
        Item {
            id: resetLink
            objectName: root.idKey !== "" ? "optionReset_" + root.idKey : ""
            visible: root.changed
            anchors.fill: parent
            activeFocusOnTab: true
            Keys.onReturnPressed: root.resetRequested()
            Keys.onEnterPressed: root.resetRequested()
            Keys.onSpacePressed: root.resetRequested()
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Reset")
                font.pixelSize: Theme.fontMeta
                font.underline: true
                color: resetLink.activeFocus ? Theme.accent : Theme.textMuted
            }
            TapHandler { onTapped: root.resetRequested() }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
        }
    }

    // ---- controls
    // Focus ring: 2px accent with a 2px gap in the app background, drawn around the control.
    component FocusRing: Item {
        property bool on: false
        property real cornerRadius: 6
        visible: on
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: parent.cornerRadius + 2
            color: "transparent"
            border.width: 2
            border.color: Theme.bg
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: parent.cornerRadius + 4
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
        }
    }

    // Select 280 x 32: menu 30 high items, at most 8 visible, then scroll.
    component RowSelect: ComboBox {
        id: cb
        property string current: ""
        signal picked(string value)
        readonly property int menuItems: 8

        implicitWidth: Theme.settingsControlWidth
        implicitHeight: Theme.settingsControlHeight
        textRole: "label"
        valueRole: "value"
        font.pixelSize: Theme.fontSmall
        leftPadding: 12
        rightPadding: 28
        currentIndex: {
            for (let i = 0; i < cb.count; ++i) {
                if (cb.model[i] && cb.model[i].value === cb.current) {
                    return i
                }
            }
            return -1
        }
        onActivated: index => cb.picked(cb.model[index].value)
        Keys.onReturnPressed: event => { if (!cb.popup.visible) { cb.popup.open() } else { event.accepted = false } }

        contentItem: Text {
            text: cb.displayText
            font: cb.font
            color: Theme.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        indicator: Text {
            x: cb.width - width - 10
            y: (cb.height - height) / 2
            text: "▾"
            font.pixelSize: Theme.fontMono
            color: Theme.textFaint
        }
        background: Rectangle {
            radius: Theme.radius6
            color: Theme.surface
            border.width: 1
            border.color: cb.popup.visible ? Theme.accent : (cb.hovered ? Theme.borderButton : Theme.borderInput)
        }
        delegate: ItemDelegate {
            id: item
            required property var modelData
            required property int index
            readonly property bool selected: item.modelData.value === cb.current
            width: ListView.view ? ListView.view.width : cb.width
            height: 30
            padding: 0
            leftPadding: 8
            rightPadding: 8
            highlighted: cb.highlightedIndex === item.index
            contentItem: RowLayout {
                spacing: 6
                Text {
                    Layout.preferredWidth: 16
                    text: item.selected ? "✓" : ""
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: Theme.fontSmall
                    color: Theme.accent
                }
                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: item.modelData.label
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    color: item.selected ? Theme.text : Theme.textSecondary
                }
            }
            background: Rectangle {
                radius: Theme.radius5
                color: item.selected ? Theme.borderInput : (item.highlighted || item.hovered ? Theme.surfaceRaised : "transparent")
            }
        }
        popup: Popup {
            y: cb.height + 4
            width: cb.width
            padding: 4
            contentItem: ColumnLayout {
                spacing: 0
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, cb.menuItems * 30)
                    clip: true
                    model: cb.popup.visible ? cb.delegateModel : null
                    currentIndex: cb.highlightedIndex
                    ScrollBar.vertical: ScrollBar { }
                }
                Text {
                    visible: cb.count > cb.menuItems
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.topMargin: 2
                    topPadding: 6
                    leftPadding: 4
                    text: qsTr("Scroll for more · type to jump")
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontMono
                    color: Theme.textFaint
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

    Component {
        id: toggleComp
        Item {
            width: 40
            height: Theme.settingsControlHeight
            FbToggle {
                id: tg
                objectName: root.toggleName !== "" ? root.toggleName : root.controlName
                anchors.verticalCenter: parent.verticalCenter
                width: 40
                height: 22
                checked: root.toggleChecked
                onToggled: {
                    root.toggled(tg.checked)
                    if (root.values.length === 2) {
                        root.valuePicked(root.values[tg.checked ? 1 : 0].value)
                    }
                }
            }
            FocusRing { anchors.fill: tg; cornerRadius: 11; on: tg.visualFocus }
        }
    }

    Component {
        id: segmentComp
        Item {
            width: root.controlWidth
            height: Theme.settingsControlHeight
            FbSegment {
                id: seg
                objectName: root.controlName
                anchors.fill: parent
                stretch: true
                segmentHeight: Theme.settingsControlHeight - 6
                options: root.values.map(function (v) { return { value: v.value, label: v.label, name: v.name || "" } })
                current: root.current
                activeFocusOnTab: true
                onPicked: value => root.valuePicked(value)
                function step(delta: int) {
                    for (let i = 0; i < root.values.length; ++i) {
                        if (root.values[i].value === root.current) {
                            const n = Math.max(0, Math.min(root.values.length - 1, i + delta))
                            if (n !== i) {
                                root.valuePicked(root.values[n].value)
                            }
                            return
                        }
                    }
                }
                Keys.onLeftPressed: seg.step(-1)
                Keys.onRightPressed: seg.step(1)
            }
            FocusRing { anchors.fill: seg; cornerRadius: Theme.radius7; on: seg.activeFocus }
        }
    }

    Component {
        id: selectComp
        Item {
            width: root.controlWidth
            height: Theme.settingsControlHeight
            RowSelect {
                id: sel
                objectName: root.controlName
                anchors.fill: parent
                model: root.values
                current: root.current
                onPicked: value => root.valuePicked(value)
            }
            FocusRing { anchors.fill: sel; cornerRadius: Theme.radius6; on: sel.visualFocus }
        }
    }

    Component {
        id: buttonsComp
        Flow {
            // Never wider than the column: wide rows right-align the buttons, narrow rows left-align and wrap.
            width: root.controlWidth
            spacing: Theme.space8
            layoutDirection: root.narrow ? Qt.LeftToRight : Qt.RightToLeft
            onChildrenRectChanged: root.buttonsHeight = childrenRect.height
            Repeater {
                model: root.narrow ? root.buttons : root.buttons.slice().reverse()
                delegate: FbButton {
                    required property var modelData
                    objectName: modelData.name || ""
                    visible: modelData.visible !== false
                    enabled: modelData.enabled !== false
                    busy: modelData.busy === true
                    busyOnClick: modelData.busyOnClick === true
                    implicitHeight: Theme.settingsControlHeight
                    implicitWidth: Math.min(contentItem.implicitWidth + 28, root.controlWidth)
                    font.pixelSize: Theme.fontSmall
                    text: modelData.text
                    onClicked: root.buttonClicked(modelData.name || "")
                }
            }
        }
    }

    Component {
        id: valueComp
        Text {
            objectName: root.controlName
            width: root.controlWidth
            height: Theme.settingsControlHeight
            text: root.valueText
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            font.family: Theme.mono
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
    }
}
