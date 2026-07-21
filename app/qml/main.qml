// main.qml — 状态栏根（QQuickView 容器内）
import QtQuick

Rectangle {
    id: root

    // taskbar 模式：嵌入原生任务栏，背景强制全透明（透出任务栏自身底色），忽略 barOpacity
    readonly property bool taskbarMode: barConfig && barConfig.barMode === "taskbar"
    // 内容实际宽度（taskbar 模式下 Bar 紧凑排列）→ BarWindow 据此收缩窗口
    implicitWidth: bar.implicitWidth

    // 色板由 ThemeManager 提供（context property themeManager.colors），
    // alpha 由 barConfig.barOpacity 控制（0.0-1.0）。保持 bg 的 RGB，只替换 alpha 通道。
    // colors 里的值是字符串，必须先经 color 类型属性隐式转换再拆 .r/.g/.b 通道
    // （字符串没有这些属性，直接取得到 undefined，Qt.rgba 会产出黑色——实测踩过）
    property color barBg: themeManager ? themeManager.colors.bg : "#1e1e2e"
    color: {
        if (taskbarMode) return "transparent"
        const op = (barConfig && barConfig.barOpacity !== undefined) ? barConfig.barOpacity : 1.0
        return Qt.rgba(barBg.r, barBg.g, barBg.b, op)
    }

    Bar {
        id: bar
        anchors.fill: parent
    }
}
