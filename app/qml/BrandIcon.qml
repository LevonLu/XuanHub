// BrandIcon.qml — 设置预览与菜单共用的原版、方框枢纽和紧凑 H 图标
import QtQuick

Item {
    id: root
    property string iconStyle: "original"
    property color glyphColor: "#ffffff"
    implicitWidth: 24
    implicitHeight: 24
    readonly property string resolvedStyle: iconStyle === "hub" || iconStyle === "compact"
                                           ? iconStyle : "original"
    readonly property real unit: Math.min(width, height) / 24
    readonly property real lineWidth: 2 * unit

    Canvas {
        id: original
        anchors.fill: parent
        visible: root.resolvedStyle === "original"
        // 相对路径同时支持源码 file:// 与宿主 qrc://，不依赖插件目录中的资源副本。
        readonly property url imageSource: Qt.resolvedUrl("../resources/xuanhub-tray-icon-64.png")
        Component.onCompleted: loadImage(imageSource)
        onImageLoaded: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: root
            function onGlyphColorChanged() { original.requestPaint() }
            function onResolvedStyleChanged() { original.requestPaint() }
        }
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!isImageLoaded(imageSource) || width <= 0 || height <= 0) return
            ctx.drawImage(imageSource, 0, 0, width, height)
            ctx.globalCompositeOperation = "source-in"
            ctx.fillStyle = root.glyphColor
            ctx.fillRect(0, 0, width, height)
            ctx.globalCompositeOperation = "source-over"
        }
    }

    // 24×24 网格。A 保留中空方框，C 将中心改为上下敞开的 H；笔画均为 2、圆头半径为 1。
    Repeater {
        model: root.resolvedStyle === "hub" ? [
            [9, 9, 4, 4], [15, 9, 20, 4], [9, 15, 4, 20], [15, 15, 20, 20]
        ] : root.resolvedStyle === "compact" ? [
            [8, 8, 8, 16], [16, 8, 16, 16], [8, 12, 16, 12],
            [8, 8, 4, 4], [16, 8, 20, 4], [8, 16, 4, 20], [16, 16, 20, 20]
        ] : []
        delegate: Item {
            required property var modelData
            readonly property real dx: modelData[2] - modelData[0]
            readonly property real dy: modelData[3] - modelData[1]
            x: root.width / 2 + (modelData[0] - 12) * root.unit
            y: root.height / 2 + (modelData[1] - 12) * root.unit
            rotation: Math.atan2(dy, dx) * 180 / Math.PI
            Rectangle {
                x: -root.lineWidth / 2
                y: -root.lineWidth / 2
                width: Math.sqrt(parent.dx * parent.dx + parent.dy * parent.dy)
                       * root.unit + root.lineWidth
                height: root.lineWidth
                radius: root.lineWidth / 2
                color: root.glyphColor
                antialiasing: true
            }
        }
    }
    Rectangle {
        visible: root.resolvedStyle === "hub"
        anchors.centerIn: parent
        width: 8 * root.unit
        height: width
        radius: 1.5 * root.unit
        color: "transparent"
        border.color: root.glyphColor
        border.width: root.lineWidth
        border.pixelAligned: false
        antialiasing: true
    }
}
