// SettingsWindow.qml — 设置窗口（M8）
// 后端：context property settingsVM（草稿属性 + pluginModel + apply/reloadFromConfig +
//       themePackNames/themePackIds 主题包列表）、themeManager（colors 色板驱动 palette）。
// 保存成功或取消都经 Window.window.close() 隐藏（窗口由 SettingsController 持有，不销毁）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root
    padding: 16
    // 圆角 + 裁切：Frameless 透明窗口下，根 Rectangle 的圆角让窗口呈圆角矩形，
    // clip 防止子项（标题栏、滚动内容）溢出圆角区域。
    clip: true
    // 小圆角半径（8px），与内部卡片 radius:6 协调，不过分突兀
    property int cornerRadius: 8

    // [dev] UI 测试驱动：--uitest 启动时 SettingsController 注入 uiTestCommands（命令
    // 数组），本 Timer 每 500ms 回放一条（click:x,y / kp:文本）；正常启动该属性
    // 不存在，Timer 空转无操作。
    Timer {
        interval: 500
        repeat: true
        running: true
        property int idx: 0
        onTriggered: {
            if (typeof uiTestCommands === "undefined" || idx >= uiTestCommands.length) return
            settingsCtl.devUiEvent(uiTestCommands[idx++])
        }
    }

    // 色板别名：统一从 themeManager.colors 取（M9 数据驱动主题）。Basic 样式经
    // palette 继承传给所有子控件，故把 colors 灌进 palette.* 让 QtQuick.Controls
    // 的表单控件也跟随主题。
    readonly property var c: themeManager ? themeManager.colors : ({
        "bg": "#1e1e2e", "panel": "#313244", "fg": "#cdd6f4", "muted": "#7f849c",
        "accent": "#89b4fa", "danger": "#f38ba8", "hover": "#45475a", "border": "#45475a",
        "placeholderText": "#7f849c", "mid": "#7f849c", "dark": "#313244", "light": "#313244"
    })

    // 插件设置草稿读取：config 无该键时用 schema default 兜底（类型化表单与自绘页共用）
    function draftValue(key, def) {
        const v = settingsVM.pluginProxy.value(key)
        return (v === undefined || v === null) ? def : v
    }

    // 非法输入计数（double/color/json 字段红框时 >0）：>0 禁用保存/应用。
    // 字段组件持 invalid 属性经此报告状态；delegate 销毁时自行归还计数。
    property int invalidFields: 0
    function setFieldInvalid(holder, invalid) {
        if (holder.invalid === invalid) return
        holder.invalid = invalid
        root.invalidFields += invalid ? 1 : -1
    }

    background: Rectangle {
        color: root.c.bg
        radius: root.cornerRadius
        // 细边框：Frameless 圆角窗口在浅色背景下加 1px border 增强边界感
        border.width: 1
        border.color: root.c.border
    }
    palette.window: root.c.bg
    palette.windowText: root.c.fg
    palette.base: root.c.bg
    palette.text: root.c.fg
    palette.button: root.c.panel
    palette.buttonText: root.c.fg
    palette.highlight: root.c.accent
    palette.placeholderText: root.c.muted
    palette.mid: root.c.muted
    palette.dark: root.c.panel
    palette.light: root.c.panel

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // ── 自绘标题栏（无边框窗口）───────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: root.c.panel   // 跟随主题，比 bg 略浅，与内容区分层

            Text {
                anchors.centerIn: parent
                text: "玄枢设置 XuanHub Settings"
                color: root.c.fg
                font.pixelSize: 13
                font.bold: true
            }

            // × 关闭按钮（右上角，hover 变 danger 色）
            Item {
                id: closeBtn
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 36; height: 36
                Rectangle {
                    anchors.fill: parent
                    color: closeMa.containsMouse ? root.c.danger : "transparent"
                }
                Text {
                    anchors.centerIn: parent
                    text: "×"
                    color: closeMa.containsMouse ? "#ffffff" : root.c.fg
                    font.pixelSize: 18
                }
                MouseArea {
                    id: closeMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Window.window.close()   // 隐藏窗口（不销毁）
                }
            }

            // 拖动移动窗口：标题栏空白处按下触发系统级窗口移动
            // （避开右侧 × 按钮区域，防误触）
            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: 36
                acceptedButtons: Qt.LeftButton
                onPressed: (mouse) => {
                    if (mouse.button === Qt.LeftButton)
                        Window.window.startSystemMove()   // QWindow::startSystemMove（Win32 SC_MOVE）
                }
            }
        }

        // ── Tab 切换栏 ──────────────────────────────
        // Basic 样式经 palette 继承：选中态用 highlight(accent)、文字用 buttonText(fg)
        TabBar {
            id: tabBar
            Layout.fillWidth: true
            spacing: 0
            TabButton { text: "外观" }
            TabButton { text: "插件" }
            TabButton { text: "通用" }
        }

        // ── Tab 内容（StackLayout 只显示当前页）─────
        StackLayout {
            id: tabStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            // ════════ 外观 Tab ════════
            ScrollView {
                id: appearanceScroll
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    width: appearanceScroll.availableWidth
                    spacing: 14

                    Text { text: "主题"; color: root.c.fg; font.pixelSize: 15; font.bold: true }
                    Rectangle {
                        Layout.fillWidth: true
                        color: root.c.panel
                        radius: 6
                        implicitHeight: themeGrid.implicitHeight + 20

                        GridLayout {
                            id: themeGrid
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 10
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10

                            Text { text: "主题包"; color: root.c.fg; Layout.fillWidth: true }
                            ComboBox {
                                model: settingsVM.themePackNames
                                currentIndex: {
                                    const ids = settingsVM.themePackIds
                                    const i = ids.indexOf(settingsVM.themeId)
                                    return i >= 0 ? i : 0
                                }
                                onActivated: {
                                    const ids = settingsVM.themePackIds
                                    if (index >= 0 && index < ids.length)
                                        settingsVM.themeId = ids[index]
                                }
                            }
                            Text { text: "模式"; color: root.c.fg; Layout.fillWidth: true }
                            ComboBox {
                                model: ["dark", "light"]
                                currentIndex: settingsVM.theme === "light" ? 1 : 0
                                onActivated: settingsVM.theme = currentText
                            }
                        }
                    }

                    Text { text: "状态栏"; color: root.c.fg; font.pixelSize: 15; font.bold: true }
                    Rectangle {
                        Layout.fillWidth: true
                        color: root.c.panel
                        radius: 6
                        implicitHeight: barGrid.implicitHeight + 20

                        GridLayout {
                            id: barGrid
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 10
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10

                            Text { text: "高度（像素）"; color: root.c.fg; Layout.fillWidth: true }
                            SpinBox {
                                from: 24; to: 200
                                value: settingsVM.barHeight
                                onValueModified: settingsVM.barHeight = value
                            }
                            Text { text: "停靠边缘"; color: root.c.fg; Layout.fillWidth: true }
                            ComboBox {
                                model: ["top", "bottom"]
                                currentIndex: settingsVM.barEdge === "bottom" ? 1 : 0
                                onActivated: settingsVM.barEdge = currentText
                            }
                            Text { text: "透明度 (%)"; color: root.c.fg; Layout.fillWidth: true }
                            SpinBox {
                                from: 20; to: 100   // 下限 20 防 bar 完全看不见
                                stepSize: 5
                                value: settingsVM.barOpacity
                                onValueModified: settingsVM.barOpacity = value
                            }
                        }
                    }
                }
            }

            // ════════ 插件 Tab ════════
            // master-detail：左 = 排序插件列表（启用在前），右 = 选中插件的设置。
            // 未启用时右侧只剩启用框；启用后才显示位置/排序与插件级设置
            //（settingsQml 自绘页 优先 > settings schema 生成表单 > 无）。
            RowLayout {
                spacing: 10

                // ── 左栏：插件列表 ──
                Rectangle {
                    Layout.preferredWidth: 210
                    Layout.fillHeight: true
                    color: root.c.panel
                    radius: 6

                    ListView {
                        id: pluginList
                        anchors.fill: parent
                        anchors.margins: 6
                        clip: true
                        spacing: 2
                        model: settingsVM.pluginModel   // SettingsPluginSortModel（排序 proxy）

                        // 选中项按 id 跟踪（勾选启用触发重排后行号会变）
                        property string currentId: ""

                        // 选中变化必须刷新右侧面板（refresh 不绑 currentId，靠显式触发）
                        onCurrentIdChanged: detailPanel.refresh()

                        function ensureSelection() {
                            if (currentId.length > 0 && model.rowForId(currentId) >= 0) return
                            currentId = model.count() > 0 ? model.roleValue(0, "id") : ""
                        }
                        Component.onCompleted: ensureSelection()
                        Connections {
                            target: pluginList.model
                            function onModelReset() { pluginList.ensureSelection() }
                        }

                        delegate: Rectangle {
                            id: pluginRow
                            width: pluginList.width
                            height: 44
                            radius: 4
                            property bool selected: pluginList.currentId === model.id
                            color: selected ? root.c.hover : "transparent"
                            border.width: selected ? 1 : 0
                            border.color: root.c.accent

                            Rectangle {   // 启用状态点
                                id: dot
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                width: 8; height: 8; radius: 4
                                color: model.enabled ? root.c.accent : root.c.muted
                            }
                            ColumnLayout {
                                anchors.left: dot.right
                                anchors.leftMargin: 8
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 0
                                Text {
                                    color: root.c.fg
                                    font.pixelSize: 13
                                    text: model.name ? model.name : model.id
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    color: root.c.muted
                                    font.pixelSize: 10
                                    text: model.id + " · " + (model.kind === "python" ? "Python" : "C++")
                                          + (model.discovered ? "" : " · 磁盘缺失")
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: pluginList.currentId = model.id
                            }
                        }
                    }
                }

                // ── 右栏：选中插件的设置 ──
                Rectangle {
                    id: detailPanel
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.c.panel
                    radius: 6

                    // 行数据快照：模型的读接口非响应式，统一在模型信号/选中变化时
                    // 重取；面板控件只绑这份快照，避免与 C++ 直读打架
                    property int currentRow: -1
                    property var rowData: ({})

                    function refresh() {
                        const m = settingsVM.pluginModel
                        currentRow = m.rowForId(pluginList.currentId)
                        if (currentRow < 0) { rowData = ({}); return }
                        rowData = {
                            "name": m.roleValue(currentRow, "name"),
                            "version": m.roleValue(currentRow, "version"),
                            "author": m.roleValue(currentRow, "author"),
                            "kind": m.roleValue(currentRow, "kind"),
                            "enabled": m.roleValue(currentRow, "enabled"),
                            "position": m.roleValue(currentRow, "position"),
                            "order": m.roleValue(currentRow, "order"),
                            "discovered": m.roleValue(currentRow, "discovered"),
                            "schema": m.roleValue(currentRow, "schema"),
                            "settingsQml": m.roleValue(currentRow, "settingsQml")
                        }
                    }
                    Component.onCompleted: refresh()

                    Connections {
                        target: settingsVM.pluginModel
                        function onDataChanged() { detailPanel.refresh() }
                        function onLayoutChanged() { detailPanel.refresh() }
                        function onModelReset() { detailPanel.refresh() }
                        function onRowsInserted() { detailPanel.refresh() }
                        function onRowsRemoved() { detailPanel.refresh() }
                    }
                    // 草稿桥始终指向当前选中插件（schema 表单与自绘页都经它读写）
                    Binding {
                        target: settingsVM.pluginProxy
                        property: "pluginId"
                        value: pluginList.currentId
                    }

                    Text {   // 空态
                        anchors.centerIn: parent
                        visible: detailPanel.currentRow < 0
                        color: root.c.muted
                        text: "在左侧选择一个插件"
                    }

                    ScrollView {
                        id: detailScroll
                        anchors.fill: parent
                        anchors.margins: 12
                        clip: true
                        visible: detailPanel.currentRow >= 0

                        ColumnLayout {
                            width: detailScroll.availableWidth
                            spacing: 10

                            // 头部：名称 + 元信息（只读）
                            Text {
                                color: root.c.fg
                                font.pixelSize: 15
                                font.bold: true
                                text: {
                                    const n = detailPanel.rowData.name
                                    return (n && n.length > 0) ? n : pluginList.currentId
                                }
                            }
                            Text {
                                color: root.c.muted
                                font.pixelSize: 11
                                text: {
                                    const d = detailPanel.rowData
                                    let s = pluginList.currentId
                                    if (d.version) s += " · v" + d.version
                                    if (d.author) s += " · " + d.author
                                    s += (d.kind === "python" ? " · Python" : " · C++")
                                    if (d.discovered === false) s += " · 磁盘缺失"
                                    return s
                                }
                            }

                            // 启用开关：未启用时下方设置区整体隐藏（右侧只剩它）
                            CheckBox {
                                text: "启用此插件"
                                checked: detailPanel.rowData.enabled === true
                                onToggled: settingsVM.pluginModel.setRoleValue(
                                               detailPanel.currentRow, "enabled", checked)
                            }

                            // ── 启用后的设置区 ──
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                visible: detailPanel.rowData.enabled === true

                                // 框架级：位置 / 排序
                                GridLayout {
                                    columns: 2
                                    columnSpacing: 12
                                    rowSpacing: 8
                                    Layout.fillWidth: true

                                    Text { text: "位置"; color: root.c.fg; Layout.fillWidth: true }
                                    ComboBox {
                                        model: ["left", "center", "right"]
                                        currentIndex: Math.max(0, indexOfValue(detailPanel.rowData.position))
                                        onActivated: settingsVM.pluginModel.setRoleValue(
                                                         detailPanel.currentRow, "position", currentText)
                                    }
                                    Text { text: "排序"; color: root.c.fg; Layout.fillWidth: true }
                                    SpinBox {
                                        from: 0; to: 99
                                        value: Number(detailPanel.rowData.order) || 0
                                        onValueModified: settingsVM.pluginModel.setRoleValue(
                                                             detailPanel.currentRow, "order", value)
                                    }
                                }

                                // 插件级设置：settingsQml 自绘页优先
                                Loader {
                                    id: customPage
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: item ? item.implicitHeight : 0
                                    source: detailPanel.rowData.settingsQml !== undefined
                                            ? detailPanel.rowData.settingsQml : ""
                                    visible: source.toString().length > 0
                                    onLoaded: if (item) item.plugin = settingsVM.pluginProxy
                                    onStatusChanged: {
                                        if (status === Loader.Error)
                                            console.warn("[Settings] 插件设置页加载失败:",
                                                         source, "（真实错误见 xuanhub.log）")
                                    }
                                }
                                // 自绘页加载失败占位（沿用 PluginSlot 语义）
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 40
                                    visible: customPage.visible && customPage.status === Loader.Error
                                    radius: 4
                                    color: "transparent"
                                    border.width: 1
                                    border.color: root.c.danger
                                    Text {
                                        anchors.centerIn: parent
                                        color: root.c.danger
                                        font.pixelSize: 12
                                        text: "⚠ 设置页加载失败（详见 xuanhub.log）"
                                    }
                                }

                                // settings schema 生成的分组表单（无自绘页时）
                                Repeater {
                                    model: customPage.visible ? [] : (detailPanel.rowData.schema || [])
                                    delegate: groupDelegate
                                }
                            }
                        }
                    }

                    // ── 分组与字段编辑器组件（schema 表单用）──
                    Component {
                        id: groupDelegate
                        ColumnLayout {
                            id: groupRoot
                            Layout.fillWidth: true
                            spacing: 6
                            property var groupData: modelData
                            property bool collapsed: false

                            Rectangle {   // 组标题（点击折叠/展开）
                                Layout.fillWidth: true
                                implicitHeight: 26
                                color: "transparent"
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: root.c.muted
                                    font.pixelSize: 12
                                    font.bold: true
                                    text: (groupRoot.collapsed ? "▸ " : "▾ ")
                                          + (groupRoot.groupData.group || "设置")
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: groupRoot.collapsed = !groupRoot.collapsed
                                }
                            }
                            Repeater {
                                model: groupRoot.collapsed ? 0 : (groupRoot.groupData.fields || [])
                                delegate: fieldDelegate
                            }
                        }
                    }

                    Component {
                        id: fieldDelegate
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            property var field: modelData

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    text: field.label || field.key
                                    color: root.c.fg
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 96
                                    elide: Text.ElideRight
                                }
                                Loader {
                                    Layout.fillWidth: true
                                    sourceComponent: {
                                        switch (field.type) {
                                            case "bool":   return boolFieldComp
                                            case "int":    return intFieldComp
                                            case "double": return doubleFieldComp
                                            case "color":  return colorFieldComp
                                            case "enum":   return enumFieldComp
                                            case "json":   return jsonFieldComp
                                            default:       return stringFieldComp
                                        }
                                    }
                                    onLoaded: item.field = field
                                }
                            }
                            Text {   // description 小字
                                visible: !!field.description
                                text: field.description || ""
                                color: root.c.muted
                                font.pixelSize: 10
                                Layout.leftMargin: 104
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Component {
                        id: boolFieldComp
                        CheckBox {
                            // field 由 Loader.onLoaded 在创建后注入：默认空对象防初值期
                            // 访问崩溃，真正的初始化等 onFieldChanged（时序坑，勿回退）
                            property var field: ({})
                            Component.onCompleted: init()
                            onFieldChanged: init()
                            function init() {
                                if (!field.key) return
                                checked = root.draftValue(field.key,
                                    field.default !== undefined ? field.default : false) === true
                            }
                            onToggled: if (field.key) settingsVM.pluginProxy.setValue(field.key, checked)
                        }
                    }

                    Component {
                        id: intFieldComp
                        SpinBox {
                            property var field: ({})
                            from: field.min !== undefined ? field.min : 0
                            to: field.max !== undefined ? field.max : 999999
                            stepSize: field.step !== undefined ? field.step : 1
                            Component.onCompleted: init()
                            onFieldChanged: init()
                            function init() {
                                if (!field.key) return
                                const v = Number(root.draftValue(field.key,
                                    field.default !== undefined ? field.default : from))
                                value = isNaN(v) ? from : v
                            }
                            onValueModified: if (field.key) settingsVM.pluginProxy.setValue(field.key, value)
                        }
                    }

                    Component {
                        id: doubleFieldComp
                        TextField {
                            property var field: ({})
                            property bool invalid: false
                            property bool _initializing: false   // init 守卫（同 stringFieldComp，防 pluginId 时序串台）
                            Component.onDestruction: if (invalid) root.invalidFields -= 1
                            validator: DoubleValidator {
                                bottom: field.min !== undefined ? field.min : -1e300
                                top: field.max !== undefined ? field.max : 1e300
                                notation: DoubleValidator.StandardNotation
                            }
                            Component.onCompleted: init()
                            onFieldChanged: init()
                            function init() {
                                if (!field.key) return
                                _initializing = true
                                text = String(root.draftValue(field.key,
                                    field.default !== undefined ? field.default : 0))
                                _initializing = false
                            }
                            onTextChanged: {
                                if (!field.key || _initializing) return
                                const ok = acceptableInput && text.length > 0
                                root.setFieldInvalid(this, !ok)
                                if (ok) settingsVM.pluginProxy.setValue(field.key, parseFloat(text))
                            }
                            color: root.c.fg
                            background: Rectangle {
                                color: root.c.bg
                                radius: 4
                                border.width: 1
                                border.color: parent.activeFocus ? root.c.accent
                                              : (parent.invalid ? root.c.danger : root.c.muted)
                            }
                        }
                    }

                    Component {
                        id: stringFieldComp
                        TextField {
                            property var field: ({})
                            // init 守卫：程序设 text 不触发 onTextChanged 回写草稿，
                            // 防止切换插件时 pluginId 时序竞态把字段写到上一个插件
                            property bool _initializing: false
                            placeholderText: field.placeholder || ""
                            Component.onCompleted: init()
                            onFieldChanged: init()
                            function init() {
                                if (!field.key) return
                                _initializing = true
                                text = String(root.draftValue(field.key,
                                    field.default !== undefined ? field.default : ""))
                                _initializing = false
                            }
                            onTextChanged: if (field.key && !_initializing) settingsVM.pluginProxy.setValue(field.key, text)
                            color: root.c.fg
                            background: Rectangle {
                                color: root.c.bg
                                radius: 4
                                border.width: 1
                                border.color: parent.activeFocus ? root.c.accent : root.c.muted
                            }
                        }
                    }

                    Component {
                        id: colorFieldComp
                        RowLayout {
                            id: colorRow
                            property var field: ({})
                            property bool invalid: false
                            property bool _initializing: false   // init 守卫（同 stringFieldComp，防 pluginId 时序串台）
                            spacing: 6
                            Component.onDestruction: if (invalid) root.invalidFields -= 1

                            // 空 = 跟随插件默认；#hex 放行；命名色放行（apply 时 C++ 终校）
                            function validText(s) {
                                if (s.length === 0) return true
                                if (/^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(s))
                                    return true
                                return /^[a-zA-Z]+$/.test(s)
                            }
                            Component.onCompleted: init()
                            onFieldChanged: init()
                            function init() {
                                if (!field.key) return
                                _initializing = true
                                colorInput.text = String(root.draftValue(field.key,
                                    field.default !== undefined ? field.default : ""))
                                _initializing = false
                            }

                            TextField {
                                id: colorInput
                                Layout.fillWidth: true
                                placeholderText: colorRow.field.placeholder || "留空跟随默认"
                                onTextChanged: {
                                    if (!colorRow.field.key || colorRow._initializing) return
                                    const ok = colorRow.validText(text)
                                    root.setFieldInvalid(colorRow, !ok)
                                    if (ok) settingsVM.pluginProxy.setValue(field.key, text)
                                }
                                color: root.c.fg
                                background: Rectangle {
                                    color: root.c.bg
                                    radius: 4
                                    border.width: 1
                                    border.color: colorInput.activeFocus ? root.c.accent
                                                  : (colorRow.invalid ? root.c.danger : root.c.muted)
                                }
                            }
                            Rectangle {   // 色块预览（非法/空 → 透明）
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 18
                                radius: 3
                                border.width: 1
                                border.color: root.c.muted
                                color: /^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/
                                       .test(colorInput.text) ? colorInput.text : "transparent"
                            }
                        }
                    }

                    Component {
                        id: enumFieldComp
                        ComboBox {
                            property var field: ({})
                            model: field.options || []
                            Component.onCompleted: init()
                            onFieldChanged: init()
                            function init() {
                                if (!field.key) return
                                const cur = String(root.draftValue(field.key,
                                    field.default !== undefined ? field.default : ""))
                                const i = (field.options || []).indexOf(cur)
                                currentIndex = i >= 0 ? i : 0
                            }
                            onActivated: if (field.key) settingsVM.pluginProxy.setValue(
                                             field.key, (field.options || [])[index])
                        }
                    }

                    Component {
                        id: jsonFieldComp
                        TextArea {
                            property var field: ({})
                            property bool invalid: false
                            Component.onDestruction: if (invalid) root.invalidFields -= 1
                            implicitHeight: Math.min(160, contentHeight + 24)
                            font.family: "Consolas"
                            font.pixelSize: 12
                            wrapMode: TextEdit.NoWrap
                            selectByMouse: true
                            color: root.c.fg
                            Component.onCompleted: init()
                            onFieldChanged: init()
                            function init() {
                                if (!field.key) return
                                const v = root.draftValue(field.key,
                                    field.default !== undefined ? field.default : null)
                                text = JSON.stringify(v, null, 2)
                            }
                            onTextChanged: {
                                if (!field.key) return
                                let parsed = null
                                let ok = true
                                try { parsed = JSON.parse(text) } catch (e) { ok = false }
                                root.setFieldInvalid(this, !ok)
                                if (ok) settingsVM.pluginProxy.setValue(field.key, parsed)
                            }
                            background: Rectangle {
                                color: root.c.bg
                                radius: 4
                                border.width: 1
                                border.color: parent.activeFocus ? root.c.accent
                                              : (parent.invalid ? root.c.danger : root.c.muted)
                            }
                        }
                    }
                }
            }

            // ════════ 通用 Tab ════════
            ScrollView {
                id: generalScroll
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    width: generalScroll.availableWidth
                    spacing: 14

                    Text { text: "系统"; color: root.c.fg; font.pixelSize: 15; font.bold: true }
                    Rectangle {
                        Layout.fillWidth: true
                        color: root.c.panel
                        radius: 6
                        implicitHeight: generalGrid.implicitHeight + 20

                        GridLayout {
                            id: generalGrid
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 10
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10

                            Text { text: "开机自启"; color: root.c.fg; Layout.fillWidth: true }
                            CheckBox {
                                checked: settingsVM.autoStart
                                onToggled: settingsVM.autoStart = checked
                            }
                        }
                    }
                }
            }
        }

        // ── 底部：错误提示 + 按钮（所有 tab 共享）──────
        Text {
            Layout.fillWidth: true
            text: settingsVM.errorMessage.length > 0 ? settingsVM.errorMessage
                : (root.invalidFields > 0 ? "存在未修正的非法输入（红框项），修正后才能保存" : "")
            color: root.c.danger
            wrapMode: Text.Wrap
            visible: text.length > 0
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }   // 左弹簧，按钮靠右
            Button {
                text: "取消"
                onClicked: Window.window.close()              // 隐藏，不保存
            }
            Button {
                text: "应用"
                // 有错时禁用（保险）；存在未修正的非法字段输入时同样禁用
                enabled: settingsVM.errorMessage.length === 0 && root.invalidFields === 0
                onClicked: settingsVM.apply()                  // 保存但不关闭
            }
            Button {
                text: "保存"
                highlighted: true
                enabled: root.invalidFields === 0
                onClicked: if (settingsVM.apply()) Window.window.close()   // 保存并关闭
            }
        }
    }
}
