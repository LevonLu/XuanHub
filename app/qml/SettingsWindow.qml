// SettingsWindow.qml — 设置窗口（M8；左侧垂直导航 + UiCard 卡片流重构）
// 后端：context property settingsVM（草稿属性 + pluginModel + apply/reloadFromConfig +
//       themePackNames/themePackIds 主题包列表）、themeManager（colors 色板驱动 palette）。
// 保存成功或取消都经 Window.window.close() 隐藏（窗口由 SettingsController 持有，不销毁）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub

Pane {
    id: root
    padding: 0   // 布局自管边距（标题栏/导航/页区各有留白）
    // 圆角 + 裁切：Frameless 透明窗口下，根 Rectangle 的圆角让窗口呈圆角矩形，
    // clip 防止子项（标题栏、滚动内容）溢出圆角区域。
    clip: true
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
    // 的表单控件也跟随主题。自绘控件（Ui*.qml）内部只引用 palette。
    readonly property var c: themeManager ? themeManager.colors : ({
        "bg": "#1e1e2e", "panel": "#313244", "fg": "#cdd6f4", "muted": "#7f849c",
        "accent": "#89b4fa", "danger": "#f38ba8", "success": "#a6e3a1", "warning": "#f9e2af",
        "hover": "#45475a", "border": "#45475a", "onAccent": "#1e1e2e",
        "placeholderText": "#7f849c", "mid": "#7f849c", "dark": "#313244", "light": "#313244"
    })

    // 页名表：导航、标题栏副标题、StackLayout 共用一份索引
    readonly property var pageNames: ["外观", "插件", "通用"]

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
    palette.highlightedText: root.c.onAccent
    palette.placeholderText: root.c.muted
    palette.mid: root.c.muted
    palette.dark: root.c.panel
    palette.light: root.c.panel

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 自绘标题栏（无边框窗口）：标题 + 当前页名副标题 + 关闭按钮 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: UiMetrics.space4
                anchors.rightMargin: UiMetrics.space2
                spacing: UiMetrics.space2

                Text {
                    text: "玄枢设置 XuanHub Settings"
                    color: root.c.fg
                    font.pixelSize: UiMetrics.fontM
                    font.bold: true
                }
                Text {   // 当前页名副标题
                    text: "· " + root.pageNames[pageStack.currentIndex]
                    color: root.c.muted
                    font.pixelSize: UiMetrics.fontS
                }
                Item { Layout.fillWidth: true }   // 弹簧，关闭按钮靠右

                // × 关闭按钮（hover 变 danger 圆角底）
                Item {
                    id: closeBtn
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    Rectangle {
                        anchors.fill: parent
                        radius: UiMetrics.radiusS
                        color: closeMa.containsMouse ? root.c.danger : "transparent"
                        Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }
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
            }

            // 拖动移动窗口：标题栏空白处按下触发系统级窗口移动
            //（右侧留出关闭按钮区域，防误触）
            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: 48
                acceptedButtons: Qt.LeftButton
                onPressed: (mouse) => {
                    if (mouse.button === Qt.LeftButton)
                        Window.window.startSystemMove()   // QWindow::startSystemMove（Win32 SC_MOVE）
                }
            }

            // 底部分隔线（半透明 border 色）
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                readonly property color bord: root.c.border
                color: Qt.rgba(bord.r, bord.g, bord.b, 0.5)
            }
        }

        // ── 主体：左侧垂直导航 + 页面区 ──
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 左侧导航（152px）：选中项 accent 左竖条 + 淡底色，hover 过渡
            Rectangle {
                Layout.preferredWidth: 152
                Layout.fillHeight: true
                color: root.c.panel

                // 选中竖条：随 currentIndex 滑动（120ms）
                Rectangle {
                    id: navIndicator
                    x: 0
                    width: 3
                    height: 22
                    radius: 1.5
                    color: root.c.accent
                    y: 12 + pageStack.currentIndex * 40 + 7   // 项高 36 + 间距 4，竖条垂直居中于项
                    Behavior on y { NumberAnimation { duration: UiMetrics.animFast; easing.type: Easing.OutCubic } }
                }

                Column {
                    id: navCol
                    x: 8
                    y: 12
                    width: parent.width - 16
                    spacing: 4

                    Repeater {
                        model: root.pageNames
                        delegate: Rectangle {
                            id: navItem
                            width: navCol.width
                            height: 36
                            radius: UiMetrics.radiusS
                            property bool selected: pageStack.currentIndex === index
                            readonly property color acc: root.c.accent
                            readonly property color fg: root.c.fg
                            color: selected ? Qt.rgba(acc.r, acc.g, acc.b, 0.14)
                                 : navMa.containsMouse ? Qt.rgba(fg.r, fg.g, fg.b, 0.06)
                                 : "transparent"
                            Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData
                                color: navItem.fg
                                font.pixelSize: UiMetrics.fontM
                                font.bold: navItem.selected
                            }
                            MouseArea {
                                id: navMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: pageStack.currentIndex = index
                            }
                        }
                    }
                }

                // 右侧分隔线
                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    readonly property color bord: root.c.border
                    color: Qt.rgba(bord.r, bord.g, bord.b, 0.5)
                }
            }

            // 页面区（StackLayout 只显示当前页，切换 120ms 淡入）
            StackLayout {
                id: pageStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: 0
                // 淡入作用于 StackLayout 自身：它只渲染当前页，整体淡入即新页淡入
                //（StackLayout 没有 currentItem 属性——那是 StackView 的，
                //  误用会刷 Cannot assign [undefined] to QObject* 告警）
                onCurrentIndexChanged: pageFade.restart()
                NumberAnimation {
                    id: pageFade
                    target: pageStack
                    property: "opacity"
                    from: 0.0
                    to: 1.0
                    duration: UiMetrics.animFast
                    easing.type: Easing.OutCubic
                }

                // ════════ 外观页 ════════
                Item {
                    ScrollView {
                        id: appearanceScroll
                        anchors.fill: parent
                        clip: true

                        ColumnLayout {
                            x: UiMetrics.space4
                            y: UiMetrics.space4
                            width: appearanceScroll.availableWidth - UiMetrics.space4 * 2
                            spacing: 14

                            UiCard {
                                Layout.fillWidth: true
                                title: "主题"
                                description: "主题包与明暗模式；保存后经热重载即时生效"

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: UiMetrics.space3
                                    Text {
                                        text: "主题包"
                                        color: root.c.fg
                                        font.pixelSize: UiMetrics.fontS
                                        Layout.preferredWidth: 120
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    UiComboBox {
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
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: UiMetrics.space3
                                    Text {
                                        text: "模式"
                                        color: root.c.fg
                                        font.pixelSize: UiMetrics.fontS
                                        Layout.preferredWidth: 120
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    UiComboBox {
                                        model: ["dark", "light"]
                                        currentIndex: settingsVM.theme === "light" ? 1 : 0
                                        onActivated: settingsVM.theme = currentText
                                    }
                                }
                            }

                            UiCard {
                                Layout.fillWidth: true
                                title: "状态栏"
                                description: "尺寸、停靠边缘与工作模式"

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: UiMetrics.space3
                                    Text {
                                        text: "高度（像素）"
                                        color: root.c.fg
                                        font.pixelSize: UiMetrics.fontS
                                        Layout.preferredWidth: 120
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    UiSpinBox {
                                        from: 24; to: 200
                                        value: settingsVM.barHeight
                                        onValueModified: settingsVM.barHeight = value
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: UiMetrics.space3
                                    Text {
                                        text: "停靠边缘"
                                        color: root.c.fg
                                        font.pixelSize: UiMetrics.fontS
                                        Layout.preferredWidth: 120
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    UiComboBox {
                                        model: ["top", "bottom"]
                                        currentIndex: settingsVM.barEdge === "bottom" ? 1 : 0
                                        onActivated: settingsVM.barEdge = currentText
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: UiMetrics.space3
                                    Text {
                                        text: "工作模式"
                                        color: root.c.fg
                                        font.pixelSize: UiMetrics.fontS
                                        Layout.preferredWidth: 120
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    UiComboBox {
                                        // appbar：占住桌面工作区；taskbar：嵌入原生任务栏左侧。
                                        // 与托盘 "任务栏模式" 勾选项写同一 config 键（bar.mode）
                                        model: ["appbar", "taskbar"]
                                        currentIndex: settingsVM.barMode === "taskbar" ? 1 : 0
                                        onActivated: settingsVM.barMode = currentText
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: UiMetrics.space3
                                    Text {
                                        text: "透明度 (%)"
                                        color: root.c.fg
                                        font.pixelSize: UiMetrics.fontS
                                        Layout.preferredWidth: 120
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    UiSpinBox {
                                        from: 20; to: 100   // 下限 20 防 bar 完全看不见
                                        stepSize: 5
                                        value: settingsVM.barOpacity
                                        onValueModified: settingsVM.barOpacity = value
                                    }
                                }
                            }
                        }
                    }
                }

                // ════════ 插件页 ════════
                // master-detail：左 = 排序插件列表（启用在前），右 = 选中插件的设置。
                // 未启用时右侧只剩启用开关；启用后才显示位置/排序与插件级设置
                //（settingsQml 自绘页 优先 > settings schema 生成表单 > 无）。
                Item {
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: UiMetrics.space4
                        spacing: UiMetrics.space3

                        // ── 左栏：插件列表 ──
                        Rectangle {
                            Layout.preferredWidth: 210
                            Layout.fillHeight: true
                            color: root.c.panel
                            radius: UiMetrics.radiusM
                            border.width: 1
                            readonly property color bord: root.c.border
                            border.color: Qt.rgba(bord.r, bord.g, bord.b, 0.45)
                            clip: true   // 行圆角不溢出面板圆角

                            ListView {
                                id: pluginList
                                anchors.fill: parent
                                anchors.margins: 6
                                clip: true
                                spacing: 4
                                model: settingsVM.pluginModel   // SettingsPluginSortModel（排序 proxy）

                                // 选中项按 id 跟踪（勾选启用触发重排后行号会变）
                                property string currentId: ""

                                // 选中变化必须刷新右侧面板（refresh 不绑 currentId，靠显式触发）。
                                // 定序铁律：草稿桥 pluginId 必须先于面板刷新指向新插件——
                                // 若 refresh 先跑，自绘设置页加载完成时 proxy 还指向上一个
                                // 插件，页面的默认值回写会落进上一个插件的草稿
                                // （sysinfo 混入 countdown 全套键的实测根因）
                                onCurrentIdChanged: {
                                    settingsVM.pluginProxy.pluginId = pluginList.currentId
                                    detailPanel.refresh()
                                }

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
                                    radius: UiMetrics.radiusS
                                    property bool selected: pluginList.currentId === model.id
                                    readonly property color acc: root.c.accent
                                    readonly property color fg: root.c.fg
                                    color: selected ? Qt.rgba(acc.r, acc.g, acc.b, 0.14)
                                         : rowMa.containsMouse ? Qt.rgba(fg.r, fg.g, fg.b, 0.06)
                                         : "transparent"
                                    Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }

                                    Rectangle {   // 选中 accent 左竖条
                                        visible: pluginRow.selected
                                        anchors.left: parent.left
                                        anchors.leftMargin: 2
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 3
                                        height: 22
                                        radius: 1.5
                                        color: pluginRow.acc
                                    }
                                    Rectangle {   // 启用状态点
                                        id: dot
                                        anchors.left: parent.left
                                        anchors.leftMargin: 12
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
                                            font.pixelSize: UiMetrics.fontM
                                            text: model.name ? model.name : model.id
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            color: root.c.muted
                                            font.pixelSize: UiMetrics.fontTiny
                                            text: model.id + " · " + (model.kind === "python" ? "Python" : "C++")
                                                  + (model.discovered ? "" : " · 磁盘缺失")
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }
                                    MouseArea {
                                        id: rowMa
                                        anchors.fill: parent
                                        hoverEnabled: true
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
                            radius: UiMetrics.radiusM
                            border.width: 1
                            readonly property color bord: root.c.border
                            border.color: Qt.rgba(bord.r, bord.g, bord.b, 0.45)

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
                                    "width": m.roleValue(currentRow, "width"),
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
                            // 草稿桥始终指向当前选中插件（schema 表单与自绘页都经它读写）。
                            // 注意：pluginId 的切换在 pluginList.onCurrentIdChanged 里显式
                            // 定序完成（必须先于 refresh），此处不再用 Binding 自动同步——
                            // Binding 与 refresh 同挂 currentId 时求值顺序不保证（时序坑）

                            // 空态：居中图标字符 + 引导文案
                            Column {
                                anchors.centerIn: parent
                                spacing: UiMetrics.space2
                                visible: detailPanel.currentRow < 0
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "⚙"
                                    color: root.c.muted
                                    font.pixelSize: 28
                                }
                                Text {
                                    color: root.c.muted
                                    font.pixelSize: UiMetrics.fontM
                                    text: "在左侧选择一个插件"
                                }
                            }

                            ScrollView {
                                id: detailScroll
                                anchors.fill: parent
                                anchors.margins: 14
                                clip: true
                                visible: detailPanel.currentRow >= 0

                                ColumnLayout {
                                    width: detailScroll.availableWidth
                                    spacing: 10

                                    // 头部：名称 + 元信息（只读）
                                    Text {
                                        color: root.c.fg
                                        font.pixelSize: UiMetrics.fontXL
                                        font.bold: true
                                        text: {
                                            const n = detailPanel.rowData.name
                                            return (n && n.length > 0) ? n : pluginList.currentId
                                        }
                                    }
                                    Text {
                                        color: root.c.muted
                                        font.pixelSize: UiMetrics.fontS
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
                                    UiSwitch {
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
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: UiMetrics.space3
                                            Text {
                                                text: "位置"
                                                color: root.c.fg
                                                font.pixelSize: UiMetrics.fontS
                                                Layout.preferredWidth: 120
                                                horizontalAlignment: Text.AlignRight
                                            }
                                            UiComboBox {
                                                model: ["left", "center", "right"]
                                                currentIndex: Math.max(0, indexOfValue(detailPanel.rowData.position))
                                                onActivated: settingsVM.pluginModel.setRoleValue(
                                                                 detailPanel.currentRow, "position", currentText)
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: UiMetrics.space3
                                            Text {
                                                text: "排序"
                                                color: root.c.fg
                                                font.pixelSize: UiMetrics.fontS
                                                Layout.preferredWidth: 120
                                                horizontalAlignment: Text.AlignRight
                                            }
                                            UiSpinBox {
                                                from: 0; to: 99
                                                value: Number(detailPanel.rowData.order) || 0
                                                onValueModified: settingsVM.pluginModel.setRoleValue(
                                                                     detailPanel.currentRow, "order", value)
                                            }
                                        }
                                        // 插件宽度：自适应（跟随内容，config width=0）/ 手动（固定像素）。
                                        // 切手动时初始值取部件当前真实宽度（PluginSlot 实时上报，
                                        // 经 settingsVM.pluginContentWidth 查询），无记录回退 100。
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: UiMetrics.space3
                                            Text {
                                                text: "宽度"
                                                color: root.c.fg
                                                font.pixelSize: UiMetrics.fontS
                                                Layout.preferredWidth: 120
                                                horizontalAlignment: Text.AlignRight
                                            }
                                            UiComboBox {
                                                model: ["自适应", "手动"]
                                                currentIndex: Number(detailPanel.rowData.width) > 0 ? 1 : 0
                                                onActivated: {
                                                    if (currentIndex === 0) {
                                                        settingsVM.pluginModel.setRoleValue(
                                                            detailPanel.currentRow, "width", 0)
                                                    } else if (!(Number(detailPanel.rowData.width) > 0)) {
                                                        let w = settingsVM.pluginContentWidth(pluginList.currentId)
                                                        if (!(w > 0)) w = 100
                                                        settingsVM.pluginModel.setRoleValue(
                                                            detailPanel.currentRow, "width", Math.min(800, w))
                                                    }
                                                }
                                            }
                                            UiSpinBox {
                                                visible: Number(detailPanel.rowData.width) > 0
                                                from: 1; to: 800
                                                value: Number(detailPanel.rowData.width) || 1
                                                onValueModified: settingsVM.pluginModel.setRoleValue(
                                                                     detailPanel.currentRow, "width", value)
                                            }
                                            Text {
                                                visible: !(Number(detailPanel.rowData.width) > 0)
                                                text: "跟随内容宽度"
                                                color: root.c.muted
                                                font.pixelSize: UiMetrics.fontTiny
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
                                            radius: UiMetrics.radiusS - 2
                                            color: "transparent"
                                            border.width: 1
                                            border.color: root.c.danger
                                            Text {
                                                anchors.centerIn: parent
                                                color: root.c.danger
                                                font.pixelSize: UiMetrics.fontS
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

                                    Rectangle {   // 组标题（点击折叠/展开，hover 淡底色）
                                        Layout.fillWidth: true
                                        implicitHeight: 28
                                        radius: UiMetrics.radiusS
                                        readonly property color fg: root.c.fg
                                        color: groupMa.containsMouse
                                               ? Qt.rgba(fg.r, fg.g, fg.b, 0.06) : "transparent"
                                        Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }
                                        Text {
                                            anchors.left: parent.left
                                            anchors.leftMargin: 8
                                            anchors.verticalCenter: parent.verticalCenter
                                            color: root.c.muted
                                            font.pixelSize: UiMetrics.fontS
                                            font.bold: true
                                            text: (groupRoot.collapsed ? "▸ " : "▾ ")
                                                  + (groupRoot.groupData.group || "设置")
                                        }
                                        MouseArea {
                                            id: groupMa
                                            anchors.fill: parent
                                            hoverEnabled: true
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
                                        spacing: UiMetrics.space2
                                        Text {
                                            text: field.label || field.key
                                            color: root.c.fg
                                            font.pixelSize: UiMetrics.fontS
                                            Layout.preferredWidth: 120
                                            horizontalAlignment: Text.AlignRight
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
                                        font.pixelSize: UiMetrics.fontTiny
                                        Layout.leftMargin: 132   // 120 label + 12 间距
                                        Layout.fillWidth: true
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }

                            Component {
                                id: boolFieldComp
                                UiSwitch {
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
                                UiSpinBox {
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
                                UiTextField {
                                    property var field: ({})
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
                                }
                            }

                            Component {
                                id: stringFieldComp
                                UiTextField {
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

                                    UiTextField {
                                        id: colorInput
                                        Layout.fillWidth: true
                                        invalid: colorRow.invalid
                                        placeholderText: colorRow.field.placeholder || "留空跟随默认"
                                        onTextChanged: {
                                            if (!colorRow.field.key || colorRow._initializing) return
                                            const ok = colorRow.validText(text)
                                            root.setFieldInvalid(colorRow, !ok)
                                            if (ok) settingsVM.pluginProxy.setValue(colorRow.field.key, text)
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
                                UiComboBox {
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
                                    // init 守卫（同 stringFieldComp）：程序设 text 会触发
                                    // onTextChanged 回写草稿，不守卫会把格式化后的值（或
                                    // 时序竞态下别的插件的值）无谓写回
                                    property bool _initializing: false
                                    Component.onDestruction: if (invalid) root.invalidFields -= 1
                                    implicitHeight: Math.min(160, contentHeight + 24)
                                    font.family: "Consolas"
                                    font.pixelSize: UiMetrics.fontS
                                    wrapMode: TextEdit.NoWrap
                                    selectByMouse: true
                                    color: root.c.fg
                                    Component.onCompleted: init()
                                    onFieldChanged: init()
                                    function init() {
                                        if (!field.key) return
                                        _initializing = true
                                        const v = root.draftValue(field.key,
                                            field.default !== undefined ? field.default : null)
                                        text = JSON.stringify(v, null, 2)
                                        _initializing = false
                                    }
                                    onTextChanged: {
                                        if (!field.key || _initializing) return
                                        let parsed = null
                                        let ok = true
                                        try { parsed = JSON.parse(text) } catch (e) { ok = false }
                                        root.setFieldInvalid(this, !ok)
                                        if (ok) settingsVM.pluginProxy.setValue(field.key, parsed)
                                    }
                                    background: Rectangle {
                                        color: root.c.bg
                                        radius: UiMetrics.radiusS
                                        border.width: 1
                                        border.color: parent.activeFocus ? root.c.accent
                                                      : (parent.invalid ? root.c.danger : root.c.muted)
                                    }
                                }
                            }
                        }
                    }
                }

                // ════════ 通用页 ════════
                Item {
                    ScrollView {
                        id: generalScroll
                        anchors.fill: parent
                        clip: true

                        ColumnLayout {
                            x: UiMetrics.space4
                            y: UiMetrics.space4
                            width: generalScroll.availableWidth - UiMetrics.space4 * 2
                            spacing: 14

                            UiCard {
                                Layout.fillWidth: true
                                title: "系统"
                                description: "登录 Windows 后自动启动玄枢状态栏"

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: UiMetrics.space3
                                    Text {
                                        text: "开机自启"
                                        color: root.c.fg
                                        font.pixelSize: UiMetrics.fontS
                                        Layout.preferredWidth: 120
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    UiSwitch {
                                        checked: settingsVM.autoStart
                                        onToggled: settingsVM.autoStart = checked
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── 底部：错误提示 + 按钮（所有页共享）──
        Rectangle {   // 顶部分隔线
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            readonly property color bord: root.c.border
            color: Qt.rgba(bord.r, bord.g, bord.b, 0.5)
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: UiMetrics.space4
            Layout.rightMargin: UiMetrics.space4
            Layout.topMargin: UiMetrics.space3
            Layout.bottomMargin: UiMetrics.space3
            spacing: UiMetrics.space2

            Text {
                Layout.fillWidth: true
                text: settingsVM.errorMessage.length > 0 ? settingsVM.errorMessage
                    : (root.invalidFields > 0 ? "存在未修正的非法输入（红框项），修正后才能保存" : "")
                color: root.c.danger
                font.pixelSize: UiMetrics.fontS
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                visible: text.length > 0
            }
            UiButton {
                variant: "ghost"
                text: "取消"
                onClicked: Window.window.close()              // 隐藏，不保存
            }
            UiButton {
                variant: "outline"
                text: "应用"
                // 有错时禁用（保险）；存在未修正的非法字段输入时同样禁用
                enabled: settingsVM.errorMessage.length === 0 && root.invalidFields === 0
                onClicked: settingsVM.apply()                  // 保存但不关闭
            }
            UiButton {
                variant: "primary"
                text: "保存"
                enabled: root.invalidFields === 0
                onClicked: if (settingsVM.apply()) Window.window.close()   // 保存并关闭
            }
        }
    }
}
