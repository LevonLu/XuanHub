// NetStat.qml — 网络上下行速率显示
import QtQuick

Item {
    id: root
    property var plugin

    width: 200
    height: 40

    Row {
        anchors.centerIn: parent
        spacing: 12

        Text {
            color: themeManager ? themeManager.colors.success : "#a6e3a1"
            font.pixelSize: 14
            text: "↓ " + (plugin ? plugin.downRate : "---")
        }
        Text {
            color: themeManager ? themeManager.colors.danger : "#f38ba8"
            font.pixelSize: 14
            text: "↑ " + (plugin ? plugin.upRate : "---")
        }
    }
}
