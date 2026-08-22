// XuanHubGlyph.qml — 不依赖宿主资源的玄枢四节点品牌图形
import QtQuick

Item {
    id: root
    property color glyphColor: "#cdd6f4"
    property color centerColor: "#313244"

    onGlyphColorChanged: canvas.requestPaint()
    onCenterColorChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true

        onPaint: {
            const ctx = getContext("2d")
            const size = Math.min(width, height)
            const ox = (width - size) / 2
            const oy = (height - size) / 2
            const points = [[0.22, 0.22], [0.78, 0.22], [0.22, 0.78], [0.78, 0.78]]

            ctx.clearRect(0, 0, width, height)
            ctx.lineCap = "round"
            ctx.lineWidth = size * 0.17
            ctx.strokeStyle = root.glyphColor
            ctx.beginPath()
            ctx.moveTo(ox + size * 0.22, oy + size * 0.22)
            ctx.lineTo(ox + size * 0.78, oy + size * 0.78)
            ctx.moveTo(ox + size * 0.78, oy + size * 0.22)
            ctx.lineTo(ox + size * 0.22, oy + size * 0.78)
            ctx.stroke()

            ctx.fillStyle = root.glyphColor
            for (let i = 0; i < points.length; ++i) {
                ctx.beginPath()
                ctx.arc(ox + size * points[i][0], oy + size * points[i][1],
                        size * 0.14, 0, Math.PI * 2)
                ctx.fill()
            }

            ctx.fillStyle = root.centerColor
            ctx.beginPath()
            ctx.arc(ox + size * 0.5, oy + size * 0.5, size * 0.14, 0, Math.PI * 2)
            ctx.fill()
        }
    }
}
