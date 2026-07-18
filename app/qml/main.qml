// main.qml — 状态栏根（QQuickView 容器内）
import QtQuick

Rectangle {
    id: root
    // 色板由 ThemeManager 提供（context property themeManager.colors）
    color: themeManager ? themeManager.colors.bg : "#1e1e2e"

    Bar {
        anchors.fill: parent
    }
}
