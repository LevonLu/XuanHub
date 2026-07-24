// PluginSlot.qml — 插件渲染槽（ListView delegate）
// 契约：加载的插件 QML 根 Item 必须声明 `property var plugin`；
// 框架加载后把 viewModel 注入到该属性。
import QtQuick

Item {
    id: wrapper
    // 高度跟随所属 ListView（即 bar 实际高度），与 config.barHeight 联动；
    // 脱离 ListView 使用时的回退值 40
    height: ListView.view ? ListView.view.height : 40
    // 宽度：config 的 width>0 时用固定宽（用户在设置里为该插件指定），
    // 否则跟随加载的插件 QML 根 Item 宽度（自适应，即默认行为）。
    // 注意：固定宽模式下 Loader 内容会被裁剪/留白，由插件 QML 自行处理对齐。
    width: model.width > 0 ? model.width
                           : (slot.item ? slot.item.width
                                        : (slot.status === Loader.Error ? errorBadge.width : 0))

    // 自然内容宽度上报：设置窗口「手动」宽度的初始值取它。
    // 手动定宽时 slot.item.width 仍是内容自然宽度（Loader 不改 item 宽度），
    // 故上报值不受定宽影响；保持声明式绑定，不做命令式赋值。
    readonly property int contentWidth: slot.item ? slot.item.width : 0
    onContentWidthChanged: pluginModel.reportContentWidth(model.id, contentWidth)

    Loader {
        id: slot
        // 锚定上下边：item 高度被调整为槽高（插件 QML 里写的 height 仅作回退）；
        // 宽度不显式指定，Loader 自动跟随 item 内容宽度
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        source: model.qmlUrl

        onLoaded: {
            if (slot.item)
                slot.item.plugin = model.viewModel
            // 注意：不要在这里给 wrapper.width 赋值——命令式赋值会销毁
            // 上面的声明式绑定，item 宽度后续变化将无法跟踪
        }
        onStatusChanged: {
            if (status === Loader.Error)
                console.warn("[PluginSlot] load failed:", source,
                             "(真实错误见 xuanhub.log 中 QML 引擎的 qWarning 输出)")
        }
    }

    // 加载失败占位符。色板取 themeManager.colors.danger
    Rectangle {
        id: errorBadge
        visible: slot.status === Loader.Error
        width: badgeText.implicitWidth + 12
        height: wrapper.height
        // colors 里的值是字符串：先经 color 属性隐式转换再拆通道（字符串无 .r/.g/.b）
        property color dangerColor: themeManager.colors.danger
        // danger 配 25% alpha 当底色（淡红告警感，不打扰）
        color: Qt.rgba(dangerColor.r, dangerColor.g, dangerColor.b, 0.25)
        radius: 4

        Text {
            id: badgeText
            anchors.centerIn: parent
            text: "⚠ " + model.id
            color: themeManager.colors.danger
            font.pixelSize: 12
        }
    }
}
