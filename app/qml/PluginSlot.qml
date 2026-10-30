// PluginSlot.qml — 插件渲染槽（ListView delegate）
// 契约：加载的插件 QML 根 Item 必须声明 `property var plugin`；
// 框架加载后把 viewModel 注入到该属性。
import QtQuick
import XuanHub

Item {
    id: wrapper
    required property var model
    property bool overflowHost: false
    property double qmlLoadStartedAt: Date.now()
    // 加载耗时是一次性指标。Python worker 会频繁发 healthChanged；若每次都用
    // Date.now() 回算，这里会错误地把插件已运行时长持续累加到“启动耗时”。
    property int qmlLoadDurationMs: -1
    property int backendLoadDurationMs: 0
    readonly property var itemStyle: themeManager && themeManager.appearance
                                     ? (themeManager.appearance.items || ({})) : ({})
    readonly property bool itemDecorated: itemStyle.decoration !== "none"
    readonly property real itemPaddingX: Number(itemStyle.paddingX || 0)
    readonly property real itemPaddingY: Number(itemStyle.paddingY || 0)

    function paletteColor(name, fallback) {
        if (name === "transparent") return "transparent"
        if (themeManager && themeManager.colors
                && themeManager.colors[name] !== undefined)
            return themeManager.colors[name]
        return fallback
    }

    function withOpacity(value, opacity) {
        const c = value
        return Qt.rgba(c.r, c.g, c.b,
                       c.a * Math.max(0, Math.min(1, Number(opacity))))
    }

    function reportHealth() {
        var next = {}
        var workerLoadDurationMs = 0
        if (model.viewModel && model.viewModel.health !== undefined) {
            var workerHealth = model.viewModel.health
            for (var key in workerHealth)
                next[key] = workerHealth[key]
            workerLoadDurationMs = Math.max(
                0, Number(workerHealth.loadDurationMs || 0))
        } else if (slot.status === Loader.Loading) {
            next.state = "starting"
        } else if (slot.status === Loader.Ready) {
            next.state = "running"
        }
        if (slot.status === Loader.Error) {
            next.state = "failed"
            next.lastError = qsTranslate("xuanhub.pluginSlot", "无法加载插件界面：%1")
                                .arg(slot.source)
        }
        if (slot.status === Loader.Ready || slot.status === Loader.Error) {
            if (wrapper.qmlLoadDurationMs < 0) {
                wrapper.qmlLoadDurationMs = Math.max(
                    1, Math.round(Date.now() - wrapper.qmlLoadStartedAt))
            }
            next.loadDurationMs = wrapper.backendLoadDurationMs
                                  + wrapper.qmlLoadDurationMs
                                  + workerLoadDurationMs
        }
        pluginModel.reportHealth(model.id, next)
    }

    Component.onCompleted: {
        backendLoadDurationMs = Number(model.loadDurationMs || 0)
        reportHealth()
    }

    Connections {
        target: model.viewModel
        ignoreUnknownSignals: true
        function onHealthChanged() { wrapper.reportHealth() }
    }
    Connections {
        target: typeof barWindow !== "undefined" ? barWindow : null
        function onBarPressed() {
            if (model.pluginContext)
                model.pluginContext.notifyBarPressed()
        }
    }
    Binding {
        target: model.pluginContext
        property: "screenGeometry"
        // QRect 的 QML 值类型不能直接转换为 QVariantMap，必须显式展开字段。
        value: typeof barScreenGeometry !== "undefined" && barScreenGeometry
               ? ({x: barScreenGeometry.x, y: barScreenGeometry.y,
                   width: barScreenGeometry.width, height: barScreenGeometry.height}) : ({})
        when: model.pluginContext !== null && model.pluginContext !== undefined
    }
    Binding {
        target: model.pluginContext
        property: "barEdge"
        value: typeof barConfig !== "undefined" && barConfig ? barConfig.barEdge : "top"
        when: model.pluginContext !== null && model.pluginContext !== undefined
    }
    Binding {
        target: model.pluginContext
        property: "popupService"
        value: typeof popupService !== "undefined" ? popupService : null
        when: model.pluginContext !== null && model.pluginContext !== undefined
    }
    Binding {
        target: model.pluginContext
        property: "actions"
        value: typeof pluginActions !== "undefined" ? pluginActions : null
        when: model.pluginContext !== null && model.pluginContext !== undefined
    }
    // 高度跟随所属 ListView（即 bar 实际高度），与 config.barHeight 联动；
    // 脱离 ListView 使用时的回退值 40
    height: overflowHost ? 40 : (ListView.view ? ListView.view.height : 40)
    // 裁剪是框架级硬保证：任何插件（固定宽内容溢出、或根 Item 宽度小于实际
    // 内容）都不允许画出槽位边界——ListView 只裁剪整列不裁剪单个 delegate，
    // 不裁剪溢出内容会直接画到相邻插件上（实测 usage_hub 固定宽 260 溢出
    // 盖住 countdown）。固定宽模式下留白/截断后的对齐由插件 QML 自行处理。
    clip: true
    // 宽度：config 的 width>0 时用固定宽（用户在设置里为该插件指定），
    // 否则跟随加载的插件 QML 根 Item 宽度（自适应，即默认行为）。
    // 注意：固定宽模式下 Loader 内容会被裁剪/留白，由插件 QML 自行处理对齐。
    // slot.item.width 用 Math.max(0, ...) 防御：插件忘声明 width 时为 0，
    // 负值会扰乱 ListView contentWidth（零宽不可见的情形由 onLoaded 告警提示）
    readonly property real naturalSlotWidth: model.width > 0 ? model.width
        : (slot.item ? Math.max(0, slot.item.width + itemPaddingX * 2)
                     : (slot.status === Loader.Error ? errorBadge.width : 0))
    width: overflowHost ? naturalSlotWidth
                        : (Number(model.effectiveWidth || 0) > 0
                           ? Number(model.effectiveWidth) : naturalSlotWidth)

    // 自然内容宽度上报：设置窗口「手动」宽度的初始值取它。
    // 手动定宽时 slot.item.width 仍是内容自然宽度（Loader 不改 item 宽度），
    // 故上报值不受定宽影响；保持声明式绑定，不做命令式赋值。
    readonly property int contentWidth: slot.item
                                        ? Math.ceil(slot.item.width + itemPaddingX * 2) : 0
    onContentWidthChanged: pluginModel.reportContentWidth(model.id, contentWidth)

    Rectangle {
        id: itemSurface
        anchors.fill: parent
        visible: wrapper.itemDecorated
        property color normalColor: wrapper.paletteColor(
            wrapper.itemStyle.background || "panel", "#313244")
        property color hoverColor: wrapper.paletteColor(
            wrapper.itemStyle.hoverBackground || "hover", "#45475a")
        color: wrapper.withOpacity(slotHover.hovered ? hoverColor : normalColor,
                                   Number(wrapper.itemStyle.opacity !== undefined
                                          ? wrapper.itemStyle.opacity : 1))
        radius: wrapper.itemStyle.decoration === "pill"
                ? height / 2 : Number(wrapper.itemStyle.radius || 0)
        border.width: Number(wrapper.itemStyle.borderWidth || 0)
        border.color: wrapper.paletteColor(
            wrapper.itemStyle.borderColor || "border", "#45475a")

        Behavior on color {
            ColorAnimation {
                duration: Number((themeManager.appearance.motion || {}).duration || 0)
            }
        }
    }

    HoverHandler { id: slotHover }

    Loader {
        id: slot
        // 锚定上下边：item 高度被调整为槽高（插件 QML 里写的 height 仅作回退）；
        // 宽度不显式指定，Loader 自动跟随 item 内容宽度
        x: wrapper.itemPaddingX
        y: wrapper.itemPaddingY
        height: Math.max(0, parent.height - wrapper.itemPaddingY * 2)
        source: model.qmlUrl

        onSourceChanged: {
            // 同一槽位切换 QML 源时开始一轮新的加载计时；后续健康更新仍复用完成值。
            wrapper.qmlLoadStartedAt = Date.now()
            wrapper.qmlLoadDurationMs = -1
        }

        onLoaded: {
            if (slot.item) {
                // 未初始化的 var 值也是 undefined；检查属性是否声明，不能检查其当前值。
                if ("pluginContext" in slot.item)
                    slot.item.pluginContext = model.pluginContext
                slot.item.plugin = model.viewModel
                // 可选布局契约：需要按胶囊实际内容区自适应的插件声明
                // hostContentHeight，框架将 Loader 已扣除内边距后的高度以绑定传入。
                // 不碰未声明该属性的既有插件，保持旧插件尺寸语义。
                if (slot.item.hostContentHeight !== undefined)
                    slot.item.hostContentHeight = Qt.binding(function() { return slot.height })
            }
            // 零宽/负宽告警：插件根 Item 忘声明正 width 时槽位宽 0，加载
            // 成功却完全不可见且无任何日志——此处显式提示（模型里有插件 id）
            if (slot.item && slot.item.width <= 0)
                console.warn("[PluginSlot]", model.id,
                             "item width =", slot.item.width,
                             "（插件根 Item 必须声明正宽度，否则槽位不可见）")
            // 注意：不要在这里给 wrapper.width 赋值——命令式赋值会销毁
            // 上面的声明式绑定，item 宽度后续变化将无法跟踪
            wrapper.reportHealth()
        }
        onStatusChanged: {
            wrapper.reportHealth()
            if (status === Loader.Error)
                console.warn("[PluginSlot] load failed:", source,
                             "(真实错误见 xuanhub.log 中 QML 引擎的 qWarning 输出)")
        }
    }

    // 加载失败占位符。色板取 themeManager.colors.danger
    Rectangle {
        id: errorBadge
        visible: slot.status === Loader.Error
        width: Math.min(320, Math.max(80, badgeText.implicitWidth + 12))
        height: wrapper.height
        // colors 里的值是字符串：先经 color 属性隐式转换再拆通道（字符串无 .r/.g/.b）
        // themeManager/colors.danger 缺失时给 fallback（参照 main.qml 的空值防御）
        property color dangerColor: (themeManager && themeManager.colors.danger !== undefined)
                                    ? themeManager.colors.danger : "#f38ba8"
        // danger 配 25% alpha 当底色（淡红告警感，不打扰）
        color: Qt.rgba(dangerColor.r, dangerColor.g, dangerColor.b, 0.25)
        radius: UiMetrics.radiusS

        Text {
            id: badgeText
            anchors.fill: parent
            anchors.margins: 6
            verticalAlignment: Text.AlignVCenter
            text: "⚠ " + model.id
                  + (String(model.lastError || "").length > 0
                     ? ": " + String(model.lastError) : "")
            // 自定义属性不能跨 item 裸名引用，必须经 id 限定（实测 ReferenceError）
            color: errorBadge.dangerColor
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }
}
