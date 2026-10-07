import QtQuick
import FrameBeam.Player

// Frame-time sparkline of the last 5 s (3t-3y): total frame time (accent, area below), emulation part (grey) and the
// dashed reference line at the core's frame time (16.7 ms at 60 fps). total/emu are arrays of milliseconds, oldest first.
Item {
    id: root
    property var total: []
    property var emu: []
    property real targetMs: 16.7
    property bool legend: true
    implicitWidth: 236
    implicitHeight: 34 + (legend ? 14 : 0)

    onTotalChanged: canvas.requestPaint()
    onEmuChanged: canvas.requestPaint()
    onTargetMsChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        objectName: "sparklineCanvas"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.legend ? root.height - 14 : root.height
        renderTarget: Canvas.Image
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var w = width, h = height
            var t = root.total || [], e = root.emu || []
            var peak = Math.max(root.targetMs * 1.6, 1)
            for (var i = 0; i < t.length; ++i) peak = Math.max(peak, t[i] * 1.1)
            function ypos(v) { return h - 1 - Math.min(v / peak, 1) * (h - 2) }
            function xpos(i, n) { return n <= 1 ? w : i * (w - 1) / (n - 1) }

            // reference line
            ctx.strokeStyle = Theme.borderButton
            ctx.lineWidth = 1
            ctx.setLineDash([3, 3])
            ctx.beginPath()
            ctx.moveTo(0, ypos(root.targetMs))
            ctx.lineTo(w, ypos(root.targetMs))
            ctx.stroke()
            ctx.setLineDash([])
            if (t.length < 2) return

            // area under the total line
            ctx.fillStyle = Qt.rgba(60 / 255, 191 / 255, 216 / 255, 0.14)
            ctx.beginPath()
            ctx.moveTo(0, h)
            for (var a = 0; a < t.length; ++a) ctx.lineTo(xpos(a, t.length), ypos(t[a]))
            ctx.lineTo(w, h)
            ctx.closePath()
            ctx.fill()

            // emulation line
            ctx.strokeStyle = Theme.textTile
            ctx.lineWidth = 1
            ctx.beginPath()
            for (var b = 0; b < e.length; ++b) {
                if (b === 0) ctx.moveTo(xpos(b, e.length), ypos(e[b]))
                else ctx.lineTo(xpos(b, e.length), ypos(e[b]))
            }
            ctx.stroke()

            // total line
            ctx.strokeStyle = Theme.accent
            ctx.lineWidth = 1.25
            ctx.beginPath()
            for (var c = 0; c < t.length; ++c) {
                if (c === 0) ctx.moveTo(xpos(c, t.length), ypos(t[c]))
                else ctx.lineTo(xpos(c, t.length), ypos(t[c]))
            }
            ctx.stroke()
        }
    }

    Row {
        visible: root.legend
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        spacing: 10
        FbMono { text: qsTr("— frame"); font.pixelSize: 10; color: Theme.textTile }
        FbMono { text: qsTr("— emu"); font.pixelSize: 10; color: Theme.textTile }
        FbMono { text: qsTr("- - %1 ms").arg(root.targetMs.toFixed(1)); font.pixelSize: 10; color: Theme.textTile }
    }
}
