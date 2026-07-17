// main.qml — 状态栏根（QQuickView 容器内）
import QtQuick

Rectangle {
    id: root
    color: {
        var t = themeManager ? themeManager.currentTheme : "dark";
        return t === "light" ? "#ececec" : "#1e1e2e";
    }

    Bar {
        anchors.fill: parent
    }
}
