import QtQuick
import Uzume.App

// Diagnostics block of the stage (board 3g-6): F3 opens the UzStageDiagnostics block in the top right corner of the stage,
// fed from the DiagnosticsModel: the emulation of your game (core, renderer, resolution, FPS, frame time, audio) and one row per
// person of a Session (connection, role). Positioned by the game screen. "Copy" puts the whole text on the clipboard, including
// the encoder / decoder and link lines and the fallback hint that do not fit the block. In the Multiview only your tile is
// emulated on this device, so the emulation rows are the same.
UzStageDiagnostics {
    id: root
    objectName: "diagnosticsOverlay"
    required property DiagnosticsModel model
    property bool multi: false
    readonly property var emu: model.emulation
    readonly property var groups: model.streaming

    // [[label, value], [label, value, "warn"]]
    readonly property var modelRows: {
        const r = []
        const e = root.emu
        if (e.valid === true) {
            r.push([qsTr("Core"), e.core || ""])
            r.push([qsTr("Renderer"), (e.renderer || "") + (e.fallback === true ? " · " + qsTr("fallback") : ""), e.fallback === true ? "warn" : undefined])
            r.push([qsTr("Resolution"), (e.resolution || "") + (e.resolutionSub ? " · " + e.resolutionSub : "")])
            r.push([qsTr("FPS"), e.fps || ""])
            r.push([qsTr("Frame"), e.frame || ""])
            r.push([qsTr("Audio"), (e.audio || "") + (e.audioSub ? " · " + e.audioSub : "")])
        } else {
            r.push([qsTr("Game"), qsTr("No game running")])
        }
        if (root.multi) {   // remote tiles are not emulated here: one row each says where the emulation runs
            const ts = root.model.tiles || []
            for (let t = 0; t < ts.length; ++t)
                if (ts[t].local !== true && ts[t].note) r.push([qsTr("Tile %1").arg(ts[t].index), ts[t].note])
        }
        if (root.groups.length === 0) {
            r.push([qsTr("Stream"), qsTr("No session")])
        } else {
            for (let g = 0; g < root.groups.length; ++g) {
                const ps = root.groups[g].participants || []
                for (let p = 0; p < ps.length; ++p)
                    r.push([ps[p].name, (ps[p].role ? ps[p].role + " · " : "") + ps[p].pill, ps[p].tone === "warn" ? "warn" : undefined])
            }
        }
        return r
    }
    rows: modelRows

    // The full text for "Copy"
    function fullText() {
        const lines = [root.title]
        for (const r of root.modelRows) lines.push(r[0] + ": " + r[1])
        if (root.emu.valid === true && root.emu.fallback === true && root.emu.fallbackText) lines.push(root.emu.fallbackText)
        for (const g of root.groups) {
            for (const p of (g.participants || [])) {
                lines.push(p.name + " (" + p.role + ") · " + p.pill)
                if (p.line2) lines.push("  " + p.line2)
                if (p.line3) lines.push("  " + p.line3)
            }
        }
        return lines.join("\n")
    }
    TextEdit { id: clip; visible: false }
    onCopyRequested: {
        clip.text = root.fullText()
        clip.selectAll()
        clip.copy()
        clip.text = ""
    }
}
