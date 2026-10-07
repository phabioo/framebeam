import QtQuick

// Player mark (docs/design/logo.md, concept 4a "Frame & Beam"): the beam arrives and becomes a play arrow.
// Tone "color": ink tile, paper frame, Signal Cyan beam (sidebar and headers).
Canvas {
    id: root
    property int size: 22
    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    onSizeChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const k = root.size / 100
        function rr(x, y, w, h, r) {
            ctx.beginPath()
            ctx.moveTo(x + r, y)
            ctx.lineTo(x + w - r, y)
            ctx.arcTo(x + w, y, x + w, y + r, r)
            ctx.lineTo(x + w, y + h - r)
            ctx.arcTo(x + w, y + h, x + w - r, y + h, r)
            ctx.lineTo(x + r, y + h)
            ctx.arcTo(x, y + h, x, y + h - r, r)
            ctx.lineTo(x, y + r)
            ctx.arcTo(x, y, x + r, y, r)
            ctx.closePath()
        }
        ctx.scale(k, k)
        // Tile
        ctx.fillStyle = "#1b1b1d"
        rr(0, 0, 100, 100, 21.5)
        ctx.fill()
        // Frame: rect 24,24 52x52, radius 6, stroke 8
        ctx.strokeStyle = "#f6f5f2"
        ctx.lineWidth = 8
        rr(24, 24, 52, 52, 6)
        ctx.stroke()
        // Beam: bar and play triangle
        ctx.fillStyle = "#3cbfd8"
        rr(18, 45.5, 28, 9, 2)
        ctx.fill()
        ctx.beginPath()
        ctx.moveTo(42, 35)
        ctx.lineTo(64, 50)
        ctx.lineTo(42, 65)
        ctx.closePath()
        ctx.fill()
    }
}
