import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Recursive renderer of a validated plugin block tree (docs: ui spec §4). `node` is the QVariantMap of one block
// ({type, id, children, ...props}); every nested block is a PluginBlock again and bubbles its actions up, so the root
// emits action(action, value) for buttons, toggles, selects, list rows, map markers etc. `dark` is true for in-game
// surfaces (always dark). Plugins choose tones, size names and player numbers only; all mapping is in Theme.
Item {
    id: root
    property var node: ({})
    property bool dark: true
    property string pluginId: ""
    signal action(string action, var value)

    readonly property string type: node.type || ""
    readonly property var kids: node.children || []
    readonly property bool fillsHeight: type === "scroll" && !node.max_height
    objectName: node.id ? "plugin_" + node.id : "plugin_" + type

    implicitWidth: loader.item ? loader.item.implicitWidth : 0
    implicitHeight: loader.item ? loader.item.implicitHeight : 0

    // Blocks of natural size stay left-aligned in a stretching column; the others take the full width.
    function stretches(t) {
        return ["chip", "badge", "avatar", "image", "button", "segmented"].indexOf(t) < 0
    }
    // Children of a row that give way when the row is narrow (text wraps or elides, containers shrink); the others keep their size.
    function shrinks(t) {
        return ["text", "stat", "bar", "select", "textfield", "location", "card", "column", "row", "grid", "list", "table"].indexOf(t) >= 0
    }
    function alignFlag(horizontal) {
        switch (node.align || (horizontal ? "stretch" : "center")) {
        case "start": return horizontal ? Qt.AlignLeft : Qt.AlignTop
        case "end": return horizontal ? Qt.AlignRight : Qt.AlignBottom
        case "center": return horizontal ? Qt.AlignHCenter : Qt.AlignVCenter
        default: return 0
        }
    }

    Loader {
        id: loader
        anchors.fill: parent
        visible: sourceComponent !== null
        sourceComponent: {
            switch (root.type) {
            case "column": return columnC
            case "row": return rowC
            case "grid": return gridC
            case "tabs": return tabsC
            case "scroll": return scrollC
            case "card": return cardC
            case "section": return sectionC
            case "text": return textC
            case "chip": return chipC
            case "badge": return badgeC
            case "avatar": return avatarC
            case "image": return imageC
            case "bar": return barC
            case "stat": return statC
            case "list": return listC
            case "table": return tableC
            case "button": return buttonC
            case "toggle": return toggleC
            case "select": return selectC
            case "segmented": return segmentedC
            case "textfield": return textfieldC
            case "empty": return emptyC
            case "location": return locationC
            case "map": return mapC
            default: return null
            }
        }
    }

    // ---- layout blocks
    Component {
        id: columnC
        ColumnLayout {
            spacing: Theme.gapPx(root.node.gap)
            Repeater {
                model: root.kids.length
                delegate: PluginChild {
                    required property int index
                    readonly property var kid: root.kids[index] || ({})
                    node: kid
                    dark: root.dark
                    pluginId: root.pluginId
                    onAction: (a, v) => root.action(a, v)
                    Layout.fillWidth: (root.node.align || "stretch") === "stretch" && (root.stretches(kid.type) || (kid.type === "image" && !!kid.label))
                    Layout.fillHeight: kid.type === "scroll" && !kid.max_height
                    Layout.alignment: (root.node.align || "stretch") === "stretch" ? ((root.stretches(kid.type) || (kid.type === "image" && !!kid.label)) ? 0 : Qt.AlignLeft) : root.alignFlag(true)
                }
            }
        }
    }
    Component {
        id: rowC
        RowLayout {
            id: rowLayout
            spacing: Theme.gapPx(root.node.gap)
            property int fillers: 0
            function countFillers() {
                var n = 0
                for (var i = 0; i < root.kids.length; ++i) {
                    const k = root.kids[i] || ({})
                    if (root.shrinks(k.type) || (k.type === "image" && !!k.label)) ++n
                }
                fillers = n
            }
            Component.onCompleted: countFillers()
            Connections { target: root; function onKidsChanged() { rowLayout.countFillers() } }
            Repeater {
                model: root.kids.length
                delegate: PluginChild {
                    required property int index
                    readonly property var kid: root.kids[index] || ({})
                    // an image with a label gives way as well: its label elides
                    readonly property bool giveWay: root.shrinks(kid.type) || (kid.type === "image" && !!kid.label)
                    node: kid
                    dark: root.dark
                    pluginId: root.pluginId
                    onAction: (a, v) => root.action(a, v)
                    Layout.fillWidth: giveWay
                    Layout.minimumWidth: giveWay ? 0 : implicitWidth
                    Layout.fillHeight: root.node.align === "stretch"
                    Layout.alignment: root.alignFlag(false)
                }
            }
            Item { visible: rowLayout.fillers === 0; Layout.fillWidth: true }
        }
    }
    Component {
        id: gridC
        GridLayout {
            columns: Math.max(1, Math.min(6, root.node.columns || 1))
            columnSpacing: Theme.gapPx(root.node.gap)
            rowSpacing: Theme.gapPx(root.node.gap)
            Repeater {
                model: root.kids.length
                delegate: PluginChild {
                    required property int index
                    readonly property var kid: root.kids[index] || ({})
                    node: kid
                    dark: root.dark
                    pluginId: root.pluginId
                    onAction: (a, v) => root.action(a, v)
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                }
            }
        }
    }
    Component {
        id: tabsC
        ColumnLayout {
            id: tabs
            spacing: 12
            property int localSel: 0
            readonly property int nodeSel: root.node.selected || 1
            readonly property int sel: Math.max(1, Math.min(root.kids.length, localSel > 0 ? localSel : nodeSel))
            onNodeSelChanged: localSel = 0
            function select(i) {   // 1-based; LB / RB hook
                const n = Math.max(1, Math.min(root.kids.length, i))
                if (n === sel) return
                localSel = n
                root.action(root.node.action || "", n)
            }
            function next() { select(sel + 1) }
            function previous() { select(sel - 1) }
            onSelChanged: Qt.callLater(tabStrip.ensureVisible)
            // The tab header never exceeds the width: a clipped, horizontally flickable strip keeps the selected tab in view.
            Flickable {
                id: tabStrip
                objectName: "pluginTabStrip"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                implicitWidth: tabRow.implicitWidth
                implicitHeight: 32
                clip: true
                contentWidth: tabRow.implicitWidth
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                function ensureVisible(index) {
                    const it = tabRepeater.itemAt((index === undefined ? tabs.sel : index) - 1)
                    if (!it) return
                    if (it.x < contentX) contentX = Math.max(0, it.x - 8)
                    else if (it.x + it.width > contentX + width) contentX = Math.min(Math.max(0, contentWidth - width), it.x + it.width - width + 8)
                }
                onWidthChanged: Qt.callLater(ensureVisible)
                Component.onCompleted: Qt.callLater(ensureVisible)
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (e) => {
                        const d = Math.abs(e.angleDelta.x) > Math.abs(e.angleDelta.y) ? e.angleDelta.x : e.angleDelta.y
                        tabStrip.contentX = Math.max(0, Math.min(Math.max(0, tabStrip.contentWidth - tabStrip.width), tabStrip.contentX - d / 2))
                    }
                }
            Row {
                id: tabRow
                spacing: 4
                Repeater {
                    id: tabRepeater
                    model: root.node.tabs || []
                    delegate: PluginFocusable {
                        id: tab
                        required property var modelData
                        required property int index
                        objectName: "pluginTab_" + (index + 1)
                        readonly property bool active: tabs.sel === index + 1
                        dark: root.dark
                        ringRadius: Theme.radius6
                        implicitHeight: 32
                        implicitWidth: tabLabel.implicitWidth + 24
                        width: implicitWidth
                        height: implicitHeight
                        onActivated: tabs.select(index + 1)
                        onActiveFocusChanged: if (activeFocus) tabStrip.ensureVisible(index + 1)
                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radius6
                            color: tab.hovered && !tab.active ? Theme.blockColor("hover", root.dark) : "transparent"
                        }
                        Text {
                            id: tabLabel
                            anchors.centerIn: parent
                            text: String(tab.modelData)
                            font.pixelSize: Theme.fontSmall
                            font.weight: tab.active ? Font.DemiBold : Font.Medium
                            color: tab.active ? Theme.blockColor("text", root.dark) : Theme.blockColor("textMuted", root.dark)
                        }
                        Rectangle {
                            visible: tab.active
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width - 16
                            height: 2
                            color: Theme.blockColor("accent", root.dark)
                        }
                    }
                }
            }
            }
            PluginChild {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                node: root.kids[tabs.sel - 1] || ({})
                dark: root.dark
                pluginId: root.pluginId
                onAction: (a, v) => root.action(a, v)
            }
        }
    }
    Component {
        id: scrollC
        Flickable {
            id: flick
            readonly property real maxH: ({s: 160, m: 280, l: 420})[root.node.max_height] || 0
            clip: true
            contentWidth: width
            contentHeight: inner.implicitHeight
            implicitWidth: inner.implicitWidth
            implicitHeight: maxH > 0 ? Math.min(maxH, contentHeight) : contentHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                contentItem: Rectangle {
                    implicitWidth: 5
                    radius: 2.5
                    color: Theme.blockColor("borderButton", root.dark)
                    opacity: parent.active ? 1 : 0.6
                }
            }
            function scrollBy(dy) {   // right stick hook
                contentY = Math.max(0, Math.min(Math.max(0, contentHeight - height), contentY + dy))
            }
            ColumnLayout {
                id: inner
                width: flick.width
                spacing: 8
                Repeater {
                    model: root.kids.length
                    delegate: PluginChild {
                        required property int index
                        readonly property var kid: root.kids[index] || ({})
                        node: kid
                        dark: root.dark
                        pluginId: root.pluginId
                        onAction: (a, v) => root.action(a, v)
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }

    // ---- containers with surface
    Component {
        id: cardC
        PluginFocusable {
            id: card
            readonly property bool hasHeader: !!(root.node.eyebrow || root.node.title || root.node.link_label)
            dark: root.dark
            actionable: !!root.node.action
            ringRadius: Theme.radius8
            implicitWidth: cardCol.implicitWidth + 24
            implicitHeight: cardCol.implicitHeight + 24
            onActivated: root.action(root.node.action, root.node.value)
            Rectangle {
                anchors.fill: parent
                radius: Theme.radius8
                color: card.hovered ? Theme.blockColor("hover", root.dark) : Theme.blockColor("card", root.dark)
                border.width: 1
                border.color: Theme.blockColor("cardBorder", root.dark)
            }
            ColumnLayout {
                id: cardCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                RowLayout {
                    visible: card.hasHeader
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            visible: !!root.node.eyebrow
                            text: root.node.eyebrow || ""
                            font.family: Theme.mono
                            font.pixelSize: Theme.fontMono
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.6
                            color: Theme.blockColor("textMeta", root.dark)
                        }
                        Text {
                            visible: !!root.node.title
                            Layout.fillWidth: true
                            text: root.node.title || ""
                            elide: Text.ElideRight
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: Theme.blockColor("text", root.dark)
                        }
                    }
                    Text {
                        visible: !!root.node.link_label
                        text: (root.node.link_label || "") + " ›"
                        font.pixelSize: Theme.fontMeta
                        color: Theme.blockColor("accent", root.dark)
                    }
                }
                Repeater {
                    model: root.kids.length
                    delegate: PluginChild {
                        required property int index
                        readonly property var kid: root.kids[index] || ({})
                        node: kid
                        dark: root.dark
                        pluginId: root.pluginId
                        onAction: (a, v) => root.action(a, v)
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
    Component {
        id: sectionC
        ColumnLayout {
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: root.node.title || ""
                    elide: Text.ElideRight
                    font.family: Theme.mono
                    font.pixelSize: Theme.fontMono
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.6
                    color: Theme.blockColor("textMeta", root.dark)
                }
                PluginFocusable {
                    visible: !!root.node.link_label
                    dark: root.dark
                    actionable: !!root.node.action
                    ringRadius: Theme.radius4
                    implicitWidth: linkText.implicitWidth + 8
                    implicitHeight: 22
                    onActivated: root.action(root.node.action, root.node.value)
                    Text {
                        id: linkText
                        anchors.centerIn: parent
                        text: (root.node.link_label || "") + " ›"
                        font.pixelSize: Theme.fontMeta
                        color: Theme.blockColor("accent", root.dark)
                    }
                }
            }
            Repeater {
                model: root.kids.length
                delegate: PluginChild {
                    required property int index
                    readonly property var kid: root.kids[index] || ({})
                    node: kid
                    dark: root.dark
                    pluginId: root.pluginId
                    onAction: (a, v) => root.action(a, v)
                    Layout.fillWidth: true
                }
            }
        }
    }
    Component {
        id: locationC
        Item {
            id: loc
            readonly property string status: root.node.status || "none"
            readonly property string tone: status === "open" || status === "encounter" ? "info" : (status === "caught" ? "ok" : "neutral")
            readonly property string statusText: status === "open" ? qsTr("Open") : status === "encounter" ? qsTr("Encounter now")
                                                : status === "caught" ? qsTr("Caught") : status === "missed" ? qsTr("Missed") : qsTr("No route")
            implicitWidth: locCol.implicitWidth + 24
            implicitHeight: locCol.implicitHeight + 24
            Rectangle {
                anchors.fill: parent
                radius: Theme.radius8
                color: Theme.blockColor("card", root.dark)
                border.width: 1
                border.color: loc.tone === "neutral" ? Theme.blockColor("cardBorder", root.dark) : Theme.toneLine(loc.tone, root.dark)
            }
            ColumnLayout {
                id: locCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: root.node.title || ""
                            elide: Text.ElideRight
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: Theme.blockColor("text", root.dark)
                        }
                        Text {
                            visible: !!root.node.sub
                            Layout.fillWidth: true
                            text: root.node.sub || ""
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontMeta
                            color: Theme.blockColor("textMeta", root.dark)
                        }
                    }
                    Rectangle {
                        objectName: "pluginLocationStatus"
                        implicitWidth: statusLabel.implicitWidth + 18
                        implicitHeight: 22
                        radius: 11
                        color: Theme.toneBgFor(loc.tone, root.dark)
                        Text {
                            id: statusLabel
                            anchors.centerIn: parent
                            text: loc.statusText
                            font.pixelSize: Theme.fontMeta
                            font.weight: Font.DemiBold
                            color: Theme.toneFg(loc.tone, root.dark)
                        }
                    }
                }
                Repeater {
                    model: root.kids.length
                    delegate: PluginChild {
                        required property int index
                        readonly property var kid: root.kids[index] || ({})
                        node: kid
                        dark: root.dark
                        pluginId: root.pluginId
                        onAction: (a, v) => root.action(a, v)
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
    Component {
        id: emptyC
        ColumnLayout {
            spacing: 6
            readonly property bool isError: root.node.tone === "danger"
            Item { Layout.preferredHeight: 6 }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: root.node.title || ""
                font.pixelSize: 15
                font.weight: Font.DemiBold
                color: isError ? Theme.toneFg("danger", root.dark) : Theme.blockColor("text", root.dark)
            }
            Text {
                visible: !!root.node.text
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: root.node.text || ""
                font.pixelSize: Theme.fontSmall
                color: Theme.blockColor("textMuted", root.dark)
            }
            PluginButton {
                visible: !!root.node.action_label
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 6
                dark: root.dark
                node: ({label: root.node.action_label || "", variant: isError ? "secondary" : "primary", action: root.node.action || ""})
                onAction: (a, v) => root.action(a, v)
            }
            Item { Layout.preferredHeight: 6 }
        }
    }

    // ---- text and small static blocks
    Component {
        id: textC
        Text {
            readonly property string st: root.node.style || "body"
            readonly property bool wraps: root.node.wrap !== false
            text: root.node.text || ""
            wrapMode: wraps ? Text.Wrap : Text.NoWrap
            elide: wraps ? Text.ElideNone : Text.ElideRight
            font.pixelSize: st === "title" ? 17 : st === "heading" ? 15 : st === "meta" ? 12 : st === "eyebrow" ? 11 : 13
            font.weight: st === "title" || st === "heading" ? Font.DemiBold : (st === "value" ? Font.Medium : Font.Normal)
            font.family: st === "eyebrow" || st === "value" ? Theme.mono : Qt.application.font.family
            font.capitalization: st === "eyebrow" ? Font.AllUppercase : Font.MixedCase
            font.letterSpacing: st === "eyebrow" ? 0.6 : 0
            color: root.node.tone ? Theme.toneFg(root.node.tone, root.dark)
                   : (st === "meta" || st === "eyebrow") ? Theme.blockColor("textMeta", root.dark)
                   : st === "body" ? Theme.blockColor("textSecondary", root.dark) : Theme.blockColor("text", root.dark)
        }
    }
    Component {
        id: chipC
        PluginFocusable {
            id: chip
            readonly property string tone: root.node.tone || "neutral"
            readonly property bool picked: root.node.selected === true
            dark: root.dark
            actionable: !!root.node.action
            ringRadius: 14
            implicitHeight: 28
            implicitWidth: chipLabel.implicitWidth + 24
            onActivated: root.action(root.node.action, root.node.value)
            Rectangle {
                anchors.fill: parent
                radius: 14
                color: chip.picked ? Theme.blockColor("text", root.dark) : (chip.hovered ? Theme.blockColor("hover", root.dark) : "transparent")
                border.width: chip.picked ? 0 : 1
                border.color: chip.tone === "neutral" ? Theme.blockColor("border", root.dark) : Theme.toneLine(chip.tone, root.dark)
            }
            Text {
                id: chipLabel
                anchors.centerIn: parent
                text: root.node.text || ""
                font.pixelSize: Theme.fontMeta
                font.weight: chip.picked ? Font.Medium : Font.Normal
                color: chip.picked ? Theme.blockColor("surface", root.dark)
                       : (chip.tone === "neutral" ? Theme.blockColor("textSecondary", root.dark) : Theme.toneFg(chip.tone, root.dark))
            }
        }
    }
    Component {
        id: badgeC
        Rectangle {
            implicitHeight: 20
            implicitWidth: badgeLabel.implicitWidth + 16
            radius: 10
            color: Theme.toneBgFor(root.node.tone || "neutral", root.dark)
            Text {
                id: badgeLabel
                anchors.centerIn: parent
                text: root.node.text || ""
                font.pixelSize: Theme.fontMono
                font.weight: Font.DemiBold
                color: Theme.toneFg(root.node.tone || "neutral", root.dark)
            }
        }
    }
    Component {
        id: avatarC
        Rectangle {
            readonly property int px: root.node.size === "s" ? 20 : (root.node.size === "l" ? 32 : 26)
            implicitWidth: px
            implicitHeight: px
            radius: px / 2
            color: Theme.playerColor(root.node.color || 1, root.dark)
            Text {
                anchors.centerIn: parent
                text: (root.node.initial || "").substring(0, 1).toUpperCase()
                font.pixelSize: Math.round(parent.px * 0.46)
                font.weight: Font.DemiBold
                color: root.dark ? "#121315" : "#ffffff"
            }
        }
    }
    Component {
        id: imageC
        RowLayout {
            spacing: 8
            PluginImage {
                node: root.node
                dark: root.dark
                pluginId: root.pluginId
            }
            Text {
                visible: !!root.node.label
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.node.label || ""
                elide: Text.ElideRight
                font.pixelSize: Theme.fontMeta
                color: Theme.blockColor("textMuted", root.dark)
            }
        }
    }
    Component {
        id: barC
        ColumnLayout {
            readonly property real v: Math.max(0, Math.min(1, root.node.value || 0))
            readonly property string tone: root.node.tone || (v > 0.5 ? "ok" : (v >= 0.2 ? "warning" : "danger"))
            spacing: 4
            Text {
                visible: !!root.node.label
                text: root.node.label || ""
                font.pixelSize: Theme.fontMeta
                color: Theme.blockColor("textMuted", root.dark)
            }
            Rectangle {
                objectName: "pluginBarTrack"
                Layout.fillWidth: true
                implicitWidth: 80
                implicitHeight: 4
                radius: 2
                color: Theme.blockColor("track", root.dark)
                Rectangle {
                    objectName: "pluginBarFill"
                    width: parent.width * parent.parent.v
                    height: parent.height
                    radius: 2
                    color: Theme.toneFg(parent.parent.tone, root.dark)
                }
            }
        }
    }
    Component {
        id: statC
        PluginFocusable {
            id: stat
            dark: root.dark
            actionable: !!root.node.action
            ringRadius: Theme.radius6
            implicitHeight: 32
            implicitWidth: statLabel.implicitWidth + statValue.implicitWidth + 40
            onActivated: root.action(root.node.action, root.node.value)
            Rectangle {
                anchors.fill: parent
                radius: Theme.radius6
                color: stat.hovered ? Theme.blockColor("hover", root.dark) : "transparent"
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 10
                Text {
                    id: statLabel
                    Layout.fillWidth: true
                    text: root.node.label || ""
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSmall
                    color: Theme.blockColor("textMuted", root.dark)
                }
                Text {
                    id: statValue
                    text: root.node.value === undefined ? "" : String(root.node.value)
                    font.family: Theme.mono
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Medium
                    color: Theme.blockColor("text", root.dark)
                }
                Text {
                    visible: !!root.node.action
                    text: "›"
                    font.pixelSize: Theme.fontCard
                    color: Theme.blockColor("textFaint", root.dark)
                }
            }
        }
    }

    // ---- blocks with their own file
    Component { id: listC; PluginList { node: root.node; dark: root.dark; pluginId: root.pluginId; onAction: (a, v) => root.action(a, v) } }
    Component { id: tableC; PluginTable { node: root.node; dark: root.dark; pluginId: root.pluginId; onAction: (a, v) => root.action(a, v) } }
    Component { id: buttonC; PluginButton { node: root.node; dark: root.dark; pluginId: root.pluginId; onAction: (a, v) => root.action(a, v) } }
    Component { id: toggleC; PluginToggle { node: root.node; dark: root.dark; pluginId: root.pluginId; onAction: (a, v) => root.action(a, v) } }
    Component { id: selectC; PluginSelect { node: root.node; dark: root.dark; pluginId: root.pluginId; onAction: (a, v) => root.action(a, v) } }
    Component { id: segmentedC; PluginSegmented { node: root.node; dark: root.dark; pluginId: root.pluginId; onAction: (a, v) => root.action(a, v) } }
    Component { id: textfieldC; PluginTextField { node: root.node; dark: root.dark; pluginId: root.pluginId; onAction: (a, v) => root.action(a, v) } }
    Component { id: mapC; PluginMapView { node: root.node; dark: root.dark; pluginId: root.pluginId; onAction: (a, v) => root.action(a, v) } }
}
