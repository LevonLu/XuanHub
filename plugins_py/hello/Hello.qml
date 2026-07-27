// Hello.qml — py_hello 子插件界面（M6 示例）。契约同 C++ 插件：根 Item 声明 property var plugin。
import QtQuick

Item {
    id: root
    property var plugin

    // 宽度跟随内容：prefix 可配，写死宽度会被槽位 clip:true 静默截断
    width: helloText.implicitWidth + 24
    height: 40

    Text {
        id: helloText
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
