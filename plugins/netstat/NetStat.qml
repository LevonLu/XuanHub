// NetStat.qml — 网络上下行速率显示（两行：下行 / 上行）
// 每行的前缀与颜色可独立配置（config.json 的 config 段：
// downPrefix/upPrefix/downColor/upColor）；颜色缺省跟随主题 success/danger。
import QtQuick

Item {
    id: root
    property var plugin

    // 宽度跟随内容（前缀可配置、长度不定），两侧留点呼吸位
    width: col.implicitWidth + 12
    height: 40

    Column {
        id: col
        anchors.centerIn: parent
        spacing: 1

        // 第一行：下行（默认 success 绿）
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: (plugin && plugin.downColor !== "")
                   ? plugin.downColor
                   : (themeManager ? themeManager.colors.success : "#a6e3a1")
            font.pixelSize: 12
            text: (plugin ? plugin.downPrefix : "↓ ") + (plugin ? plugin.downRate : "---")
        }

        // 第二行：上行（默认 danger 红）
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: (plugin && plugin.upColor !== "")
                   ? plugin.upColor
                   : (themeManager ? themeManager.colors.danger : "#f38ba8")
            font.pixelSize: 12
            text: (plugin ? plugin.upPrefix : "↑ ") + (plugin ? plugin.upRate : "---")
        }
    }
}
