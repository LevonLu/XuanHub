// PluginSlot.qml — 插件渲染槽（ListView delegate）
// 契约：加载的插件 QML 根 Item 必须声明 `property var plugin`；
// 框架加载后把 viewModel 注入到该属性。
import QtQuick

Item {
    id: wrapper
    height: 40
    width: slot.item ? slot.item.width : 0

    Loader {
        id: slot
        anchors.verticalCenter: parent.verticalCenter
        source: model.qmlUrl

        onLoaded: {
            if (slot.item)
                slot.item.plugin = model.viewModel
            // 注意：不要在这里给 wrapper.width 赋值——命令式赋值会销毁
            // 上面的声明式绑定，item 宽度后续变化将无法跟踪
        }
        onStatusChanged: {
            if (status === Loader.Error)
                console.warn("[PluginSlot] load failed:", source)
        }
    }
}
