// PluginPopup.qml — 插件共享的顶层弹窗定位、互斥与关闭状态机
import QtQuick
import QtQuick.Window

Window {
    id: popup

    property Item anchorItem
    property var pluginContext
    property real gap: 6
    property int revealDelay: 50
    property bool closeOnDeactivate: true
    property bool closeOnBarPress: true
    property bool wasOpenOnBarPress: false

    transientParent: null
    visible: false
    opacity: 0
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    function screenGeometry() {
        if (pluginContext && pluginContext.screenGeometry)
            return pluginContext.screenGeometry
        return (typeof barScreenGeometry !== "undefined") ? barScreenGeometry : null
    }

    function barEdge() {
        if (pluginContext && pluginContext.barEdge)
            return pluginContext.barEdge
        return (typeof barConfig !== "undefined" && barConfig) ? barConfig.barEdge : "top"
    }

    function reposition() {
        if (!anchorItem || typeof popupService === "undefined") {
            opacity = 1
            return
        }
        const topLeft = anchorItem.mapToGlobal(Qt.point(0, 0))
        const screen = screenGeometry()
        if (!screen || Number(screen.width) <= 0) {
            opacity = 1
            return
        }
        const result = popupService.calculate({
            x: topLeft.x,
            y: topLeft.y,
            width: anchorItem.width,
            height: anchorItem.height
        }, screen, width, height, gap, barEdge())
        x = Math.round(result.x)
        y = Math.round(result.y)
        revealTimer.restart()
    }

    function openAnchored() {
        if (!anchorItem) {
            console.warn("[PluginPopup] anchorItem is required")
            return
        }
        if (typeof popupService !== "undefined")
            popupService.activate(popup)
        opacity = 0
        visible = true
        placementTimer.restart()
        raise()
        requestActivate()
    }

    // 在插件 bar MouseArea.onClicked 中调用。barPressed 信号先到时，本方法会消费
    // 同一次点击，避免“刚关闭又重开”；点击另一个插件则直接打开并由服务关闭旧弹窗。
    function toggleFromBar() {
        if (wasOpenOnBarPress) {
            wasOpenOnBarPress = false
            return
        }
        openAnchored()
    }

    function closePopup() {
        hide()
    }

    onActiveChanged: {
        if (closeOnDeactivate && !active && visible)
            hide()
    }
    onVisibleChanged: {
        if (visible) {
            placementTimer.restart()
        } else {
            opacity = 0
            if (typeof popupService !== "undefined")
                popupService.release(popup)
        }
    }
    onWidthChanged: if (visible) placementTimer.restart()
    onHeightChanged: if (visible) placementTimer.restart()

    Connections {
        target: popup.pluginContext
                ? popup.pluginContext
                : ((typeof barWindow !== "undefined") ? barWindow : null)
        function onBarPressed() {
            popup.wasOpenOnBarPress = popup.visible
            if (popup.closeOnBarPress && popup.visible)
                popup.hide()
        }
    }

    Shortcut {
        sequence: "Escape"
        onActivated: popup.hide()
    }

    Timer {
        id: placementTimer
        interval: 0
        onTriggered: popup.reposition()
    }

    Timer {
        id: revealTimer
        interval: Math.max(0, popup.revealDelay)
        onTriggered: popup.opacity = 1
    }
}
