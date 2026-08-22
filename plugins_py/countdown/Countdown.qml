// Countdown.qml — py_countdown 子插件界面
// 显示「前缀 HH:MM:SS 后缀」；倒计时归零（reached）时切到「到点文字」并高亮
// （用户填了 reachedColor 用它，否则回退主题成功色）。契约同 intime/hello。
import QtQuick

Item {
    id: root
    property var plugin
    property var pluginContext

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

    readonly property color cFg:      themeManager ? themeManager.colors.fg      : "#cdd6f4"
    readonly property color cSuccess: themeManager ? themeManager.colors.success : "#a6e3a1"

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
}
