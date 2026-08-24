// main.qml — 状态栏根（QQuickView 容器内）
import QtQuick

Item {
    id: root

    // 内容实际宽度（taskbar 模式下 Bar 紧凑排列）→ BarWindow 据此收缩窗口
    implicitWidth: bar.implicitWidth

    Bar {
        id: bar
        anchors.fill: parent
    }
}
