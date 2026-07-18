// main.qml — 状态栏根（QQuickView 容器内）
import QtQuick

Rectangle {
    id: root
    // 色板由 ThemeManager 提供（context property themeManager.colors），
    // alpha 由 barConfig.barOpacity 控制（0.0-1.0）。保持 bg 的 RGB，只替换 alpha 通道。
    color: {
        const base = themeManager ? themeManager.colors.bg : "#1e1e2e"
        const op = (barConfig && barConfig.barOpacity !== undefined) ? barConfig.barOpacity : 1.0
        return Qt.rgba(base.r, base.g, base.b, op)
    }

    Bar {
        anchors.fill: parent
    }
}
