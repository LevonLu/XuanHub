// Hello.qml — py_hello 子插件界面（M6 示例）。契约同 C++ 插件：根 Item 声明 property var plugin。
import QtQuick

Item {
    id: root
    property var plugin

    width: 190
    height: 40

    Text {
        anchors.centerIn: parent
        color: "#a6e3a1"
        font.pixelSize: 16
        text: (plugin && plugin.values.text) ? plugin.values.text : "py: ..."
    }

    // 点击 → python invoke("bump")，计数 +1（M6.4 交互验证）
    MouseArea {
        anchors.fill: parent
        onClicked: if (plugin) plugin.call("bump", [])
    }
}
