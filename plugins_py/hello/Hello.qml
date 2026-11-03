// Hello.qml — py_hello 子插件界面（M6 示例）。契约同 C++ 插件：根 Item 声明 property var plugin。
import QtQuick

Item {
    id: root
    property var plugin
    property var pluginContext

    // 宽度跟随内容：prefix 可配，写死宽度会被槽位 clip:true 静默截断
    width: helloText.implicitWidth + 24
    height: 40

    // 色板别名：统一从 themeManager.colors 取（M9 数据驱动主题），缺失时给深色兜底
    readonly property var colors: pluginContext ? pluginContext.colors
                                                : (themeManager ? themeManager.colors : ({
        "bg": "#202020", "panel": "#2b2b2b", "fg": "#ffffff", "muted": "#cecece",
        "accent": "#60cdff", "danger": "#ff99a4", "success": "#6ccb5f", "warning": "#fce100",
        "hover": "#383838", "border": "#484848", "onAccent": "#202020"
    }))

    // bar hover 反馈（提示可点击）；clickArea 在下方声明，运行时解析无碍
    property color hoverColor: root.colors.hover
    Rectangle {
        anchors.fill: parent
        radius: 6
        color: Qt.rgba(root.hoverColor.r, root.hoverColor.g, root.hoverColor.b,
                       clickArea.containsMouse ? 0.35 : 0)
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    Text {
        id: helloText
        anchors.centerIn: parent
        color: root.colors.success
        font.pixelSize: 16
        text: (plugin && plugin.values.text) ? plugin.values.text : "py: ..."
    }

    // 点击 → python invoke("bump")，计数 +1（M6.4 交互验证）
    MouseArea {
        id: clickArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: if (plugin) plugin.call("bump", [])
    }
}
