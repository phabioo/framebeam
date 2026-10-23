import QtQuick
import QtQuick.Layouts
import Uzume.App

// Server › Network & sharing (accounts-and-server.md 3q-app-2) from GET /admin/settings (network, security): the done line, Share over
// the internet (toggle, the server asks Confirm it's you), Public address, Ports (Copy), Relay (TURN) Built-in / External / Off,
// Security code (Show, Copy, Renew...), the other ports, the relay status, External TURN servers (Add / Edit / Remove / use the server
// flags again), STUN servers and the table of every network value with where it comes from (Use the server environment value).
// Values that need a restart show the pane's "Restart the server to apply" line.
// Left out, the API carries nothing for it: the server's other UZUME_* variables (only the network values have a running / saved / env
// state), the Cloudflare TURN token (a flag, shown only as On / Off).
// objectNames: settingsNetwork, networkDone, networkSharingRow, networkSharing, networkPublicRow, networkPublicChange, networkPortsRow,
// networkPortsCopy, networkRelayRow, networkRelay (segmented: networkRelayBuiltin / networkRelayExternal / networkRelayOff), networkCertRow,
// networkCertShow, networkCertCopy, networkCertRenew, networkCertDialog, networkCertConfirm, networkListenRow, networkTurnPortRow,
// networkRelayPortsRow, networkRelayIpRow, networkExternalTable, externalAdd, externalEdit, externalRemove, externalReset, externalDialog,
// externalName, externalUrls, externalUsername, externalSecret, externalClear, externalSave, externalRemoveDialog, externalRemoveConfirm,
// stunTable, stunAdd, stunRemove, stunNone, valuesTable, valuesReset.
ColumnLayout {
    id: root
    objectName: "settingsNetwork"
    required property var pane
    readonly property var net: root.pane.network
    readonly property var ext: root.net.external_turn ?? ({ servers: [], active: false, source: "flags" })
    readonly property var turn: root.net.turn ?? null
    readonly property bool sharingOn: root.pane.isOn(root.pane.netValue("network_sharing"))
    readonly property bool turnOn: root.pane.isOn(root.pane.netValue("turn"))
    readonly property int extCount: (root.ext.servers ?? []).length
    readonly property string publicHost: root.pane.netValue("public_host")
    readonly property string relayMode: root.turnOn ? "builtin" : (root.extCount > 0 ? "external" : "off")
    readonly property bool prepared: root.publicHost !== "" && (root.turnOn || (root.ext.active === true && root.extCount > 0))
    property bool showCert: false
    property int extIndex: -1
    property int valueIndex: -1
    property string extError: ""
    property bool extBusy: false
    property string extMode: "secret"
    property string extOriginal: ""
    readonly property var extRows: (root.ext.servers ?? []).map(s => ({ name: s.name, mode: s.mode === "secret" ? qsTr("Shared secret") : qsTr("Static"),
                                                                       urls: (s.urls ?? []).join(" "), secret: s.secret_set ? qsTr("Set") : qsTr("Not set") }))
    readonly property var extSelected: root.extIndex >= 0 && root.extIndex < root.extCount ? root.ext.servers[root.extIndex] : null
    readonly property var stunList: root.stunOf(root.pane.netValue("ice_servers"))
    readonly property bool stunDefault: root.stunMode(root.pane.netValue("ice_servers")) === "default"
    readonly property var valueRows: (root.net.settings ?? []).map(s => ({ key: s.key, label: s.label, running: root.show(s, s.running_value),
                                                                          saved: root.show(s, s.value),
                                                                          source: s.source === "web" ? qsTr("Saved here") : qsTr("Server environment"),
                                                                          note: s.restart_pending ? qsTr("Restart to apply") : "" }))
    readonly property var valueSelected: root.valueIndex >= 0 && root.valueIndex < root.valueRows.length ? (root.net.settings ?? [])[root.valueIndex] : null
    spacing: Theme.space4

    Component.onCompleted: root.pane.helpItems = [
        { title: qsTr("Sharing over the internet"), text: qsTr("Apps outside your home network need a public address, a forwarded port on your router and a relay for strict networks.") },
        { title: qsTr("Restart"), text: qsTr("A change that needs a restart is saved at once and applies after the next restart; the server tells you above.") }
    ]

    // ------------------------------------------------------------ helpers
    function stunMode(raw: string): string {
        if (raw === "none") return "none"
        try {
            const l = JSON.parse(raw)
            return Array.isArray(l) && l.length > 0 ? "list" : "default"
        } catch (e) { return "default" }
    }
    function stunOf(raw: string): var {
        try {
            const l = JSON.parse(raw)
            return Array.isArray(l) ? l : []
        } catch (e) { return [] }
    }
    function show(s: var, v: string): string {
        if (s.key === "turn" || s.key === "network_sharing") return root.pane.isOn(v) ? qsTr("On") : qsTr("Off")
        if (s.key === "ice_servers") {
            const m = root.stunMode(v)
            return m === "none" ? qsTr("None") : m === "default" ? qsTr("Default") : root.stunOf(v).join(" ")
        }
        return v !== "" ? v : qsTr("Not set")
    }
    function portsText(): string {
        const p = (k) => root.pane.netSetting(k).running_value
        return qsTr("%1 TCP · %2 UDP+TCP · %3 UDP").arg(p("listen_port")).arg(p("turn_port")).arg(p("turn_relay_ports"))
    }
    function certDate(iso: string): string { return root.pane.dayText(iso) }
    function setRelay(mode: string) {
        if (mode === "builtin") {
            root.pane.saveNet("turn", { value: "1" }, qsTr("The built-in relay is on."))
        } else if (mode === "external") {
            if (root.extCount === 0) {
                root.openExternal(-1)
            } else {
                root.pane.saveNet("turn", { value: "0" }, qsTr("Apps use the external relay."))
            }
        } else {
            root.pane.saveNet("turn", { value: "0" }, root.extCount > 0 ? qsTr("The built-in relay is off. The external servers below stay in use.")
                                                                         : qsTr("The relay is off."))
        }
    }
    function editNet(key: string, title: string, text: string, label: string, mono: bool, placeholder: string) {
        const s = root.pane.netSetting(key)
        root.pane.openEdit({
            title: title, text: text, label: label, value: s.value, mono: mono, placeholder: placeholder, action: qsTr("Change"),
            save: function (v) { root.pane.saveNet(key, { value: v }, qsTr("Saved %1.").arg(label.toLowerCase())) }
        })
    }
    function editRelayPorts() {
        const parts = root.pane.netValue("turn_relay_ports").split("-")
        root.pane.openEdit({
            title: qsTr("Change the relay ports"), text: qsTr("Forward the whole range as UDP to the server. At most 1000 ports."),
            label: qsTr("First port"), label2: qsTr("Last port"), value: parts[0] ?? "", value2: parts[1] ?? "", mono: true, action: qsTr("Change"),
            save: function (a, b) { root.pane.saveNet("turn_relay_ports", { min: a, max: b }, qsTr("Saved the relay ports.")) }
        })
    }
    function renewCert() {
        root.pane.setBusy("cert", true)
        root.pane.call("POST", "/admin/security/renew-cert", undefined, function (ok, data, code, message) {
            root.pane.setBusy("cert", false)
            if (ok && data && data.fingerprint !== undefined) {
                const s = Object.assign({}, root.pane.settings)
                const sec = Object.assign({}, root.pane.security)
                sec.fingerprint = data.fingerprint
                sec.cert_not_after = data.not_after
                s.security = sec
                root.pane.settings = s
                root.pane.say(qsTr("Made a new certificate. Apps ask to confirm the new security code."), "done")
            } else {
                root.pane.say(root.pane.failText(qsTr("Could not renew the certificate"), message), "error")
            }
        })
    }
    function addStun() {
        root.pane.openEdit({
            title: qsTr("Add a STUN server"), text: qsTr("Apps use it to find their public address. Without an entry the default is used; adding one replaces it."),
            label: qsTr("Address"), value: "", mono: true, placeholder: "stun:stun.example.com:3478", action: qsTr("Add"),
            save: function (v) { root.pane.saveNet("ice_servers", { op: "add", url: v }, qsTr("Added the STUN server.")) }
        })
    }

    // external TURN
    function openExternal(index: int) {
        const sv = index >= 0 ? root.ext.servers[index] : null
        root.extError = ""
        root.extBusy = false
        root.extOriginal = sv ? sv.name : ""
        root.extMode = sv ? sv.mode : "secret"
        extName.text = sv ? sv.name : ""
        extUrls.text = sv ? (sv.urls ?? []).join("\n") : ""
        extUser.text = sv ? (sv.username ?? "") : ""
        extSecret.text = ""
        extClear.checked = false
        extDialog.open()
    }
    function saveExternal() {
        const body = { name: extName.text.trim(), mode: root.extMode,
                       urls: extUrls.text.split(/[\s,]+/).filter(u => u !== ""),
                       username: root.extMode === "static" ? extUser.text.trim() : "" }
        if (extSecret.text !== "") body.secret = extSecret.text
        if (root.extOriginal !== "") {
            body.original = root.extOriginal
            if (extClear.checked) body.clear_secret = true
        }
        root.extError = ""
        root.extBusy = true
        root.pane.call("PUT", "/admin/settings/external-turn", body, function (ok, data, code, message) {
            root.extBusy = false
            if (ok && data && data.servers !== undefined) {
                root.pane.applyExternal(data)
                extDialog.close()
                root.pane.say(root.extOriginal !== "" ? qsTr("Saved %1.").arg(body.name) : qsTr("Added %1.").arg(body.name), "done")
            } else {
                root.extError = message !== "" ? message : qsTr("The server did not answer.")
            }
        })
    }
    function removeExternal(name: string) {
        root.pane.call("DELETE", "/admin/settings/external-turn?name=" + encodeURIComponent(name), undefined, function (ok, data, code, message) {
            if (ok && data && data.servers !== undefined) {
                root.pane.applyExternal(data)
                root.extIndex = -1
                root.pane.say(qsTr("Removed %1.").arg(name), "done")
            } else {
                root.pane.say(root.pane.failText(qsTr("Could not remove %1").arg(name), message), "error")
            }
        })
    }
    function resetExternal() {
        root.pane.call("POST", "/admin/settings/external-turn/reset", undefined, function (ok, data, code, message) {
            if (ok && data && data.servers !== undefined) {
                root.pane.applyExternal(data)
                root.extIndex = -1
                root.pane.say(qsTr("The external servers are back to the server flags."), "done")
            } else {
                root.pane.say(root.pane.failText(qsTr("Could not reset the external servers"), message), "error")
            }
        })
    }

    // ------------------------------------------------------------ top block
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space2
        UzStatusIcon {
            objectName: "networkDone"
            visible: root.prepared
            kind: "done"
            text: qsTr("Prepared for the internet: public address and relay are set.")
            maxWidth: 640
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            UzSettingsRow {
                objectName: "networkSharingRow"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                resetMode: "none"
                idKey: "sharing"
                label: qsTr("Share over the internet")
                description: qsTr("Off: only apps on your home network can reach the server. Turning it on or off asks you to confirm it's you.")
                badge: root.pane.netSetting("network_sharing").restart_pending ? qsTr("applies after restart") : ""
                ctrl: "toggle"
                controlName: "networkSharing"
                checked: root.sharingOn
                onToggled: value => root.pane.saveNet("network_sharing", { value: value ? "1" : "0" },
                                                      value ? qsTr("Sharing over the internet is on.") : qsTr("Sharing over the internet is off."),
                                                      function (ok) { if (!ok) root.pane.load() })
            }
            UzSettingsRow {
                objectName: "networkPublicRow"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                resetMode: "none"
                idKey: "public"
                label: qsTr("Public address")
                descriptionName: "networkPublic"
                description: root.publicHost !== "" ? root.publicHost : qsTr("Not set. Apps outside your network can't find the server.")
                badge: root.pane.netSetting("public_host").restart_pending ? qsTr("applies after restart") : ""
                ctrl: "buttons"
                buttons: [{ name: "networkPublicChange", text: qsTr("Change…") }]
                onButtonClicked: root.editNet("public_host", qsTr("Change the public address"),
                                              qsTr("Handed to apps outside your network. Forward the listen port on your router. If the router uses another external port, add it (host:port)."),
                                              qsTr("Public address"), true, "server.example.com:8443")
            }
            UzSettingsRow {
                objectName: "networkPortsRow"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                resetMode: "none"
                idKey: "ports"
                label: qsTr("Ports")
                descriptionName: "networkPorts"
                description: root.portsText() + ((root.turn && (root.turn.forwards ?? []).length > 0) ? "\n" + qsTr("Router forwards: %1").arg(root.turn.forwards.join(" · ")) : "")
                ctrl: "buttons"
                buttons: [{ name: "networkPortsCopy", text: qsTr("Copy") }]
                onButtonClicked: root.pane.copyText(root.portsText())
            }
            UzSettingsRow {
                objectName: "networkRelayRow"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                resetMode: "none"
                idKey: "relay"
                label: qsTr("Relay (TURN)")
                description: qsTr("Relays the picture for apps that can't connect directly (strict networks, mobile data). Built-in uses this server's upload. External uses your own servers below.")
                ctrl: "segment"
                controlName: "networkRelay"
                values: [
                    { value: "builtin", label: qsTr("Built-in"), name: "networkRelayBuiltin" },
                    { value: "external", label: qsTr("External"), name: "networkRelayExternal" },
                    { value: "off", label: qsTr("Off"), name: "networkRelayOff" }
                ]
                current: root.relayMode
                onValuePicked: value => root.setRelay(value)
            }
            UzSettingsRow {
                objectName: "networkCertRow"
                visible: root.pane.security.fingerprint !== undefined
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                resetMode: "none"
                idKey: "cert"
                label: qsTr("Security code")
                descriptionName: "networkCert"
                description: (root.showCert ? root.pane.security.fingerprint : qsTr("Hidden"))
                             + (root.pane.security.cert_not_after ? "\n" + qsTr("Valid until %1").arg(root.certDate(root.pane.security.cert_not_after)) : "")
                ctrl: "buttons"
                buttons: [
                    { name: "networkCertShow", text: root.showCert ? qsTr("Hide") : qsTr("Show") },
                    { name: "networkCertCopy", text: qsTr("Copy"), visible: root.showCert },
                    { name: "networkCertRenew", text: qsTr("Renew…"), visible: root.pane.security.can_renew_cert === true }
                ]
                onButtonClicked: name => {
                    if (name === "networkCertShow") root.showCert = !root.showCert
                    else if (name === "networkCertCopy") root.pane.copyText(root.pane.security.fingerprint)
                    else certDialog.open()
                }
            }
        }
    }

    // ------------------------------------------------------------ ports and relay values
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space1
        UzSectionLabel { Layout.fillWidth: true; text: qsTr("Ports and relay") }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: [
                    { key: "listen_port", id: "listen", label: qsTr("Listen port"), row: "networkListenRow", mono: "8443",
                      text: qsTr("TCP port for apps. Changing it restarts the server."), title: qsTr("Change the listen port") },
                    { key: "turn_port", id: "turnport", label: qsTr("TURN port"), row: "networkTurnPortRow", mono: "3478",
                      text: qsTr("UDP and TCP. Forward it to the server."), title: qsTr("Change the TURN port") },
                    { key: "turn_relay_ip", id: "relayip", label: qsTr("Relay IP"), row: "networkRelayIpRow", mono: "203.0.113.10",
                      text: qsTr("A fixed public IPv4 address for relayed addresses. Empty: taken from the public address."), title: qsTr("Change the relay IP") }
                ]
                delegate: UzSettingsRow {
                    required property var modelData
                    readonly property var st: root.pane.netSetting(modelData.key)
                    objectName: modelData.row
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    resetMode: "none"
                    idKey: modelData.id
                    label: modelData.label
                    description: (st.value !== "" ? st.value + " · " : "") + modelData.text
                    badge: st.restart_pending ? qsTr("applies after restart") : ""
                    ctrl: "buttons"
                    buttons: [{ name: "networkChange_" + modelData.id, text: qsTr("Change…") }]
                    onButtonClicked: root.editNet(modelData.key, modelData.title, modelData.text, modelData.label, true, modelData.mono)
                }
            }
            UzSettingsRow {
                objectName: "networkRelayPortsRow"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                resetMode: "none"
                idKey: "relayports"
                label: qsTr("Relay ports (UDP)")
                description: root.pane.netValue("turn_relay_ports") + " · " + qsTr("Forward the whole range to the server.")
                badge: root.pane.netSetting("turn_relay_ports").restart_pending ? qsTr("applies after restart") : ""
                ctrl: "buttons"
                buttons: [{ name: "networkChange_relayports", text: qsTr("Change…") }]
                onButtonClicked: root.editRelayPorts()
            }
            UzSettingsRow {
                objectName: "networkRelayStatusRow"
                visible: root.turn !== null
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                resetMode: "none"
                idKey: "relaystatus"
                label: qsTr("Relay status")
                description: root.turn && root.turn.relay_ip ? qsTr("Relay address %1 (%2).").arg(root.turn.relay_ip).arg(root.turn.fixed ? qsTr("set by you") : qsTr("from the public address"))
                                                             : qsTr("The relay address is not known yet. Relayed Sessions fail until the public address has an IPv4 address.")
                ctrl: "value"
                controlName: "networkRelayStatus"
                valueText: root.turn ? qsTr("%1 active").arg(Theme.plural(root.turn.allocations, qsTr("allocation"), qsTr("allocations"))) : ""
            }
            UzSettingsRow {
                objectName: "networkCloudflareRow"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                resetMode: "none"
                idKey: "cloudflare"
                label: qsTr("Cloudflare TURN")
                description: qsTr("Set with a server flag, not here. The token is never shown.")
                ctrl: "value"
                controlName: "networkCloudflare"
                valueText: root.net.cloudflare_configured === true ? qsTr("On") : qsTr("Off")
            }
        }
    }

    // ------------------------------------------------------------ external TURN servers
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space1
        RowLayout {
            Layout.fillWidth: true
            UzSectionLabel { Layout.fillWidth: true; text: qsTr("External TURN servers · %1").arg(root.extCount) }
            UzButton { objectName: "externalAdd"; size: "sm"; text: qsTr("Add server…"); onClicked: root.openExternal(-1) }
            UzButton {
                objectName: "externalReset"
                size: "sm"
                kind: "ghost"
                visible: root.ext.source === "web"
                text: qsTr("Use the server flags again")
                onClicked: root.resetExternal()
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontMeta
            lineHeight: 18
            lineHeightMode: Text.FixedHeight
            color: Theme.textMeta
            text: qsTr("Your own coturn server or a TURN provider, for apps that can't connect directly. Secrets are write-only: the server never shows them again.")
                  + (root.ext.source === "web" ? " " + qsTr("This list is saved here and replaces the server flags.") : " " + qsTr("Showing the list from the server flags. Saving a change stores it here."))
                  + (root.ext.active !== true && root.extCount > 0 ? " " + qsTr("Not handed to apps while sharing over the internet is off.") : "")
        }
        UzTable {
            objectPrefix: "networkExternalTable"
            Layout.fillWidth: true
            emptyText: qsTr("No external TURN server.")
            currentIndex: root.extIndex
            onPicked: index => root.extIndex = index
            columns: [
                { key: "name", title: qsTr("Name"), fill: 1 },
                { key: "mode", title: qsTr("Mode"), width: 120 },
                { key: "urls", title: qsTr("Addresses"), fill: 2, mono: true },
                { key: "secret", title: qsTr("Secret"), width: 80 }
            ]
            rows: root.extRows
            ColumnLayout {
                objectName: "externalDetails"
                visible: root.extSelected !== null
                width: parent ? parent.width : 0
                spacing: Theme.space1
                UzSectionLabel { Layout.fillWidth: true; Layout.topMargin: Theme.space2; text: root.extSelected ? root.extSelected.name : "" }
                RowLayout {
                    spacing: Theme.space1
                    UzButton { objectName: "externalEdit"; size: "sm"; text: qsTr("Edit…"); onClicked: root.openExternal(root.extIndex) }
                    UzButton { objectName: "externalRemove"; size: "sm"; kind: "danger"; text: qsTr("Remove…"); onClicked: externalRemoveDialog.open() }
                }
            }
        }
    }

    // ------------------------------------------------------------ STUN
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space1
        RowLayout {
            Layout.fillWidth: true
            UzSectionLabel { Layout.fillWidth: true; text: qsTr("STUN servers") }
            UzButton { objectName: "stunAdd"; size: "sm"; text: qsTr("Add server…"); onClicked: root.addStun() }
            UzButton {
                objectName: "stunNone"
                size: "sm"
                kind: "ghost"
                visible: root.stunMode(root.pane.netValue("ice_servers")) !== "none"
                text: qsTr("Use no server")
                onClicked: root.pane.saveNet("ice_servers", { op: "none" }, qsTr("No STUN server is used now."))
            }
            UzButton {
                objectName: "stunDefault"
                size: "sm"
                kind: "ghost"
                visible: root.pane.netSetting("ice_servers").source === "web"
                text: qsTr("Use the default")
                onClicked: root.pane.resetNet("ice_servers", qsTr("The STUN servers are back to the default."))
            }
        }
        UzTable {
            objectPrefix: "stunTable"
            Layout.fillWidth: true
            selectable: false
            emptyText: root.stunMode(root.pane.netValue("ice_servers")) === "none"
                       ? qsTr("No STUN server. Apps only learn their public address from the built-in relay.") : qsTr("The default STUN server is used.")
            columns: [{ key: "url", title: qsTr("Address"), fill: 1, mono: true }, { key: "act", title: "", width: 120, align: "right", custom: true }]
            rows: root.stunList.map(u => ({ url: u, act: u }))
            cellDelegate: Item {
                property var row: null
                property var column: null
                implicitHeight: Theme.row32
                height: implicitHeight
                UzButton {
                    objectName: "stunRemove"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    size: "sm"
                    kind: "ghost"
                    text: qsTr("Remove")
                    onClicked: root.pane.saveNet("ice_servers", { op: "remove", url: row.url }, qsTr("Removed the STUN server."))
                }
            }
        }
    }

    // ------------------------------------------------------------ where values come from
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space1
        UzSectionLabel { Layout.fillWidth: true; text: qsTr("Values") }
        UzTable {
            objectPrefix: "valuesTable"
            Layout.fillWidth: true
            currentIndex: root.valueIndex
            onPicked: index => root.valueIndex = index
            columns: [
                { key: "label", title: qsTr("Setting"), fill: 1.2 },
                { key: "running", title: qsTr("Running"), fill: 1.4, mono: true },
                { key: "source", title: qsTr("From"), fill: 1 },
                { key: "note", title: "", fill: 0.8 }
            ]
            rows: root.valueRows
            ColumnLayout {
                objectName: "valuesDetails"
                visible: root.valueSelected !== null
                width: parent ? parent.width : 0
                spacing: Theme.space1
                UzSectionLabel { Layout.fillWidth: true; Layout.topMargin: Theme.space2; text: root.valueSelected ? root.valueSelected.label : "" }
                UzMono {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: root.valueSelected ? qsTr("After a restart: %1 · Server environment: %2").arg(root.show(root.valueSelected, root.valueSelected.value)).arg(root.show(root.valueSelected, root.valueSelected.default_value)) : ""
                }
                UzButton {
                    objectName: "valuesReset"
                    size: "sm"
                    visible: root.valueSelected !== null && root.valueSelected.source === "web"
                    text: qsTr("Use the server environment value")
                    onClicked: root.pane.resetNet(root.valueSelected.key, qsTr("%1 is back to the server's own value.").arg(root.valueSelected.label))
                }
            }
        }
        Repeater {
            model: root.net.issues ?? []
            delegate: UzStatusIcon {
                required property string modelData
                kind: "warning"
                small: true
                text: modelData
                maxWidth: 640
            }
        }
    }

    // ------------------------------------------------------------ dialogs
    UzDialog {
        id: certDialog
        objectName: "networkCertDialog"
        parent: root.pane.overlayParent
        label: qsTr("Server settings")
        title: qsTr("Make a new security code?")
        text: qsTr("The server gets a new certificate at once. Every app asks you to confirm the new security code before it connects again.")
        actions: [UzButton { text: qsTr("Cancel"); onClicked: certDialog.close() }]
        dangerActions: [
            UzButton {
                objectName: "networkCertConfirm"
                kind: "danger"
                text: qsTr("Renew")
                onClicked: { certDialog.close(); root.renewCert() }
            }
        ]
    }
    UzDialog {
        id: externalRemoveDialog
        objectName: "externalRemoveDialog"
        parent: root.pane.overlayParent
        label: qsTr("Server settings")
        title: qsTr("Remove %1?").arg(root.extSelected ? root.extSelected.name : "")
        text: qsTr("Apps no longer use this relay. Its secret is deleted from this server.")
        actions: [UzButton { text: qsTr("Cancel"); onClicked: externalRemoveDialog.close() }]
        dangerActions: [
            UzButton {
                objectName: "externalRemoveConfirm"
                kind: "danger"
                text: qsTr("Remove")
                onClicked: { const n = root.extSelected.name; externalRemoveDialog.close(); root.removeExternal(n) }
            }
        ]
    }
    Binding { target: extDialog; property: "visible"; value: false; when: root.pane.admin.confirmNeeded }
    UzDialog {
        id: extDialog
        objectName: "externalDialog"
        parent: root.pane.overlayParent
        size: "lg"
        label: qsTr("Server settings")
        title: root.extOriginal !== "" ? qsTr("Edit %1").arg(root.extOriginal) : qsTr("Add an external TURN server")
        text: qsTr("Shared secret: the server makes short-lived credentials (coturn use-auth-secret), leave the user name empty. Static: the user name and password are sent as they are.")
        error: root.extError
        busy: root.extBusy
        UzSegmented {
            objectName: "externalMode"
            options: [{ value: "secret", label: qsTr("Shared secret"), name: "externalModeSecret" }, { value: "static", label: qsTr("Static"), name: "externalModeStatic" }]
            current: root.extMode
            onPicked: value => root.extMode = value
        }
        UzField { id: extName; objectName: "externalName"; Layout.fillWidth: true; fill: Theme.ground; placeholderText: qsTr("Name") }
        UzField { id: extUrls; objectName: "externalUrls"; Layout.fillWidth: true; fill: Theme.ground; mono: true; placeholderText: "turn:turn.example.com:3478" }
        UzField { id: extUser; objectName: "externalUsername"; visible: root.extMode === "static"; Layout.fillWidth: true; fill: Theme.ground; placeholderText: qsTr("User name") }
        UzField {
            id: extSecret
            objectName: "externalSecret"
            Layout.fillWidth: true
            fill: Theme.ground
            echoMode: TextInput.Password
            placeholderText: root.extOriginal !== "" ? qsTr("Secret or password (empty keeps the stored one)") : qsTr("Secret or password")
        }
        UzCheckbox { id: extClear; objectName: "externalClear"; visible: root.extOriginal !== ""; text: qsTr("Delete the stored secret or password") }
        actions: [
            UzButton {
                objectName: "externalSave"
                kind: "primary"
                text: root.extOriginal !== "" ? qsTr("Save") : qsTr("Add")
                onClicked: root.saveExternal()
            },
            UzButton { text: qsTr("Cancel"); onClicked: extDialog.close() }
        ]
    }
}
