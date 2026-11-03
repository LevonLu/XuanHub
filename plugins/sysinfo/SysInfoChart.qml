// SysInfoChart.qml — 轻量双序列折线图，支持固定百分比与自动速率量程
import QtQuick

Canvas {
    id: root
    property var primarySeries: []
    property var secondarySeries: []
    property color primaryColor: "#60cdff"
    property color secondaryColor: "#ff99a4"
    property color gridColor: "#484848"
    property real maximum: 100
    property bool fillPrimary: true

    antialiasing: true
    onPrimarySeriesChanged: requestPaint()
    onSecondarySeriesChanged: requestPaint()
    onPrimaryColorChanged: requestPaint()
    onSecondaryColorChanged: requestPaint()
    onGridColorChanged: requestPaint()
    onMaximumChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function resolvedMaximum() {
        if (maximum > 0) return maximum
        let value = 1
        const lists = [primarySeries || [], secondarySeries || []]
        for (let n = 0; n < lists.length; ++n)
            for (let i = 0; i < lists[n].length; ++i)
                value = Math.max(value, Number(lists[n][i]) || 0)
        return value * 1.12
    }

    function trace(ctx, values, color, maxValue, fill) {
        if (!values || values.length === 0) return
        const usableWidth = Math.max(1, width - 2)
        const usableHeight = Math.max(1, height - 4)
        const xAt = i => 1 + (values.length <= 1 ? usableWidth
                                                : usableWidth * i / (values.length - 1))
        const yAt = value => 2 + usableHeight * (1 - Math.min(maxValue,
                            Math.max(0, Number(value) || 0)) / maxValue)

        if (fill && values.length > 1) {
            const gradient = ctx.createLinearGradient(0, 0, 0, height)
            gradient.addColorStop(0, Qt.rgba(color.r, color.g, color.b, 0.24))
            gradient.addColorStop(1, Qt.rgba(color.r, color.g, color.b, 0.01))
            ctx.beginPath()
            ctx.moveTo(xAt(0), yAt(values[0]))
            for (let i = 1; i < values.length; ++i) ctx.lineTo(xAt(i), yAt(values[i]))
            ctx.lineTo(xAt(values.length - 1), height)
            ctx.lineTo(xAt(0), height)
            ctx.closePath()
            ctx.fillStyle = gradient
            ctx.fill()
        }

        ctx.beginPath()
        ctx.moveTo(xAt(0), yAt(values[0]))
        for (let i = 1; i < values.length; ++i) ctx.lineTo(xAt(i), yAt(values[i]))
        ctx.strokeStyle = color
        ctx.lineWidth = 2
        ctx.lineJoin = "round"
        ctx.lineCap = "round"
        ctx.stroke()
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.strokeStyle = gridColor
        ctx.lineWidth = 1
        ctx.globalAlpha = 0.42
        for (let i = 0; i <= 4; ++i) {
            const y = Math.round(i * height / 4) + 0.5
            ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
        }
        for (let i = 0; i <= 8; ++i) {
            const x = Math.round(i * width / 8) + 0.5
            ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
        }
        ctx.globalAlpha = 1
        const maxValue = resolvedMaximum()
        trace(ctx, primarySeries, primaryColor, maxValue, fillPrimary)
        trace(ctx, secondarySeries, secondaryColor, maxValue, false)
    }
}
