// Clock.qml — 时钟显示。契约：根 Item 声明 property var plugin。
import QtQuick

Item {
    id: root
    property var plugin

    width: 130
    height: 40

    Text {
        anchors.centerIn: parent
        color: "#cdd6f4"
        font.pixelSize: 16
        text: plugin ? plugin.timeText : "--:--:--"
    }

    // 右键菜单示例（M4 交互验证）
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: console.log("[Clock] right-clicked")
    }
}
