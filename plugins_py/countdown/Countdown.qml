// Countdown.qml — py_countdown 子插件界面
// 显示「前缀 HH:MM:SS 后缀」；倒计时归零（reached）时切到「到点文字」并高亮
// （用户填了 reachedColor 用它，否则回退主题成功色）。契约同 intime/hello。
import QtQuick

Item {
    id: root
    property var plugin

    width: 200; height: 40

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
        anchors.centerIn: parent
        color: root.displayColor
        font.pixelSize: 13
        text: (plugin && plugin.values && plugin.values.text) ? plugin.values.text : "..."
    }
}
