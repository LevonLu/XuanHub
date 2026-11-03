// SysInfoIcon.qml — CPU、内存、GPU 与网络指标的主题化矢量图标
import QtQuick

Canvas {
    id: root
    property string metric: "cpu"
    property color iconColor: "#60cdff"

    antialiasing: true
    onMetricChanged: requestPaint()
    onIconColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        const w = width
        const h = height
        const s = Math.min(w, h)
        ctx.reset()
        ctx.strokeStyle = iconColor
        ctx.fillStyle = iconColor
        ctx.lineWidth = Math.max(1.2, s * 0.11)
        ctx.lineCap = "round"
        ctx.lineJoin = "round"

        if (metric === "cpu") {
            const x = w * 0.24, y = h * 0.24, size = s * 0.52
            ctx.strokeRect(x, y, size, size)
            ctx.strokeRect(w * 0.38, h * 0.38, s * 0.24, s * 0.24)
            for (let i = 0; i < 3; ++i) {
                const p = 0.32 + i * 0.18
                ctx.beginPath(); ctx.moveTo(w * p, h * 0.08); ctx.lineTo(w * p, y); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(w * p, y + size); ctx.lineTo(w * p, h * 0.92); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(w * 0.08, h * p); ctx.lineTo(x, h * p); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(x + size, h * p); ctx.lineTo(w * 0.92, h * p); ctx.stroke()
            }
        } else if (metric === "mem") {
            ctx.strokeRect(w * 0.10, h * 0.25, w * 0.80, h * 0.50)
            for (let i = 0; i < 3; ++i)
                ctx.strokeRect(w * (0.21 + i * 0.22), h * 0.36, w * 0.13, h * 0.22)
            for (let i = 0; i < 4; ++i) {
                const x = w * (0.20 + i * 0.20)
                ctx.beginPath(); ctx.moveTo(x, h * 0.75); ctx.lineTo(x, h * 0.90); ctx.stroke()
            }
        } else if (metric === "gpu") {
            ctx.strokeRect(w * 0.08, h * 0.20, w * 0.78, h * 0.58)
            ctx.beginPath(); ctx.arc(w * 0.47, h * 0.49, s * 0.19, 0, Math.PI * 2); ctx.stroke()
            ctx.beginPath(); ctx.arc(w * 0.47, h * 0.49, s * 0.05, 0, Math.PI * 2); ctx.fill()
            ctx.beginPath(); ctx.moveTo(w * 0.86, h * 0.31); ctx.lineTo(w * 0.95, h * 0.31); ctx.stroke()
            ctx.beginPath(); ctx.moveTo(w * 0.86, h * 0.55); ctx.lineTo(w * 0.95, h * 0.55); ctx.stroke()
        } else {
            const down = metric === "netDown"
            const top = down ? h * 0.20 : h * 0.80
            const bottom = down ? h * 0.80 : h * 0.20
            ctx.beginPath()
            ctx.moveTo(w * 0.50, top)
            ctx.lineTo(w * 0.50, bottom)
            ctx.moveTo(w * 0.24, down ? h * 0.56 : h * 0.44)
            ctx.lineTo(w * 0.50, bottom)
            ctx.lineTo(w * 0.76, down ? h * 0.56 : h * 0.44)
            ctx.stroke()
        }
    }
}
