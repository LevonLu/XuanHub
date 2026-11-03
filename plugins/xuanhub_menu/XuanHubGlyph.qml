// XuanHubGlyph.qml — 复用托盘位图并按当前主题着色的品牌图形
import QtQuick

Item {
    id: root
    property color glyphColor: "#ffffff"

    Canvas {
        id: canvas
        anchors.fill: parent
        readonly property url imageSource: "xuanhub-tray-icon-64.png"

        Component.onCompleted: loadImage(imageSource)
        onImageLoaded: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: root
            function onGlyphColorChanged() { canvas.requestPaint() }
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!isImageLoaded(imageSource) || width <= 0 || height <= 0) return

            ctx.drawImage(imageSource, 0, 0, width, height)
            // PNG 提供透明蒙版，source-in 只在可见轮廓内填入主题前景色。
            ctx.globalCompositeOperation = "source-in"
            ctx.fillStyle = root.glyphColor
            ctx.fillRect(0, 0, width, height)
            ctx.globalCompositeOperation = "source-over"
        }
    }
}
