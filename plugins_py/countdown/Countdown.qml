// Countdown.qml — py_countdown 子插件界面
// 显示「前缀 HH:MM:SS 后缀」；倒计时归零（reached）时切到「到点文字」并高亮
// （用户填了 reachedColor 用它，否则回退主题成功色）。契约同 intime/hello。
import QtQuick
import QtQuick.Window

Item {
    id: root
    property var plugin
    property var pluginContext

    readonly property string tooltipText: {
        var values = plugin && plugin.values ? plugin.values : ({})
        var template = values.tooltipTemplate || ""
        if (template.indexOf("{payday}") < 0 && template.indexOf("{paydayDays}") < 0)
            return template
        var ready = values.paydayStatus === "ready"
        var unknown = qsTranslate("plugin.py_countdown.main", "待确认")
        var text = template.replace(/\{payday\}/g, ready ? values.paydayDate : unknown)
                           .replace(/\{paydayDays\}/g, ready ? String(values.paydayDays) : unknown)
        if (values.paydayStatus === "calendarUnavailable")
            text += "\n" + qsTranslate("plugin.py_countdown.main", "%1 年节假日日历未收录，请更新日历后确认发薪日。")
                                  .arg(values.paydayCalendarYear)
        else if (!ready)
            text += "\n" + qsTranslate("plugin.py_countdown.main", "未找到可用工作日，请检查发薪日设置。")
        return text
    }
    onTooltipTextChanged: {
        if (!tooltipText.trim()) { tipDelay.stop(); tip.hide() }
    }

    function formatDuration(seconds) {
        var total = Math.max(0, Number(seconds) || 0)
        var days = Math.floor(total / 86400)
        var rem = total % 86400
        var hours = Math.floor(rem / 3600)
        rem %= 3600
        var minutes = Math.floor(rem / 60)
        var secs = Math.floor(rem % 60)
        function pad(v) { return v < 10 ? "0" + v : String(v) }
        var clock = pad(hours) + ":" + pad(minutes) + ":" + pad(secs)
        return days > 0
                ? qsTranslate("plugin.py_countdown.main", "%1 天 %2").arg(days).arg(clock)
                : clock
    }

    function displayText() {
        if (!plugin || !plugin.values) return "..."
        var values = plugin.values
        if (values.textKey === "invalidDailyTime")
            return qsTranslate("plugin.py_countdown.main", "⚠ 目标时间格式错误（HH:MM）")
        if (values.textKey === "invalidDateTime")
            return qsTranslate("plugin.py_countdown.main", "⚠ 目标时间格式错误（YYYY-MM-DD HH:MM）")
        if (values.reached) return values.text || "..."
        var parts = [values.prefix || "", formatDuration(values.remainingSeconds),
                     values.suffix || ""]
        return parts.filter(function(value) { return value.length > 0 }).join(" ")
    }

    // 宽度随内容（原为固定 200，倒计时文字短时槽位大片留白，
    // 在 bar 上会与右侧插件拉开异常间距）
    width: label.implicitWidth + 16; height: 40

    readonly property var hostColors: pluginContext ? pluginContext.colors
                                                    : (typeof themeManager !== "undefined" ? themeManager.colors : ({}))
    readonly property color cFg:      hostColors.fg || "#ffffff"
    readonly property color cSuccess: hostColors.success || "#6ccb5f"

    // 到点 → 自定义色（非空）或主题成功色；未到点 → 前景色
    readonly property color displayColor: {
        if (plugin && plugin.values && plugin.values.reached) {
            var rc = plugin.values.reachedColor
            if (rc && String(rc).length > 0) return rc
            return cSuccess
        }
        return cFg
    }

    Text {
        id: label
        anchors.centerIn: parent
        color: root.displayColor
        font.pixelSize: 13
        text: root.displayText()
    }

    HoverHandler {
        id: hover
        onHoveredChanged: {
            if (hovered && root.tooltipText.trim()) tipDelay.restart()
            else { tipDelay.stop(); tip.hide() }
        }
    }
    Timer {
        id: tipDelay
        interval: 400
        onTriggered: {
            if (!hover.hovered || !root.tooltipText.trim()) return
            tip.opacity = 0
            tip.reposition()
            tip.show()
            tipPlacement.restart()
        }
    }
    Connections {
        target: root.pluginContext || null
        function onBarPressed() { tipDelay.stop(); tip.hide() }
    }

    // 这是一层不接收输入、不参与互斥的提示，避免悬停抢焦点或关闭其他插件的交互弹窗。
    // Controls ToolTip 会被矮状态栏约束并遮住控件；独立 Window 复用宿主定位计算，
    // 保留 PluginPopup 相同的最终尺寸重定位，且不调用 activate/requestActivate。
    Window {
        id: tip
        objectName: "countdownTooltip"
        transientParent: null
        visible: false
        color: "transparent"
        flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
               | Qt.WindowTransparentForInput | Qt.WindowStaysOnTopHint
        readonly property var availableScreen: root.pluginContext ? root.pluginContext.screenGeometry : null
        width: Math.min(tipLabel.implicitWidth + 20, availableScreen ? Math.max(40, availableScreen.width - 16) : 440, 440)
        height: Math.min(tipLabel.implicitHeight + 16, availableScreen ? Math.max(24, availableScreen.height - 16) : 600)

        function reposition() {
            if (!root.pluginContext || !root.pluginContext.popupService || !availableScreen) return
            var anchor = root.mapToGlobal(Qt.point(0, 0))
            var pos = root.pluginContext.popupService.calculate(
                {x: anchor.x, y: anchor.y, width: root.width, height: root.height},
                availableScreen, width, height, 6, root.pluginContext.barEdge)
            x = Math.round(pos.x)
            y = Math.round(pos.y)
        }
        onWidthChanged: if (visible) tipPlacement.restart()
        onHeightChanged: if (visible) tipPlacement.restart()
        Rectangle {
            anchors.fill: parent
            clip: true
            radius: 6
            color: root.hostColors.panel || "#2b2b2b"
            border.color: root.hostColors.border || "#484848"
            border.width: 1
            Text {
                id: tipLabel
                objectName: "countdownTooltipLabel"
                x: 10; y: 8
                width: tip.width - 20
                text: root.tooltipText
                textFormat: Text.PlainText
                color: root.cFg
                font.pixelSize: 12
                wrapMode: Text.Wrap
                maximumLineCount: 20
                elide: Text.ElideRight
            }
        }
    }
    Timer {
        id: tipPlacement
        interval: 50
        onTriggered: if (tip.visible) { tip.reposition(); tip.opacity = 1 }
    }
}
