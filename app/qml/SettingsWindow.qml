// SettingsWindow.qml — Fluent 2 风格设置窗口：导航、分组表单与插件详情
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
    property int cornerRadius: UiMetrics.radiusL
    function trText(source) { return qsTranslate("xuanhub.settings", source) }

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

    Popup {
        id: dependencyConfirm
        property string pluginId: ""
        property var plan: ({})
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.min(520, root.width - UiMetrics.space6 * 2)
        x: Math.max(0, (root.width - width) / 2)
        y: Math.max(0, (root.height - height) / 2)
        padding: UiMetrics.space5
        palette.window: root.c.bg
        palette.windowText: root.c.fg
        palette.base: root.c.bg
        palette.text: root.c.fg
        palette.button: root.c.panel
        palette.buttonText: root.c.fg
        palette.highlight: root.c.accent
        palette.highlightedText: root.c.onAccent
        palette.mid: root.c.muted
        background: Rectangle {
            color: root.c.panel
            radius: UiMetrics.radiusL
            border.width: 1
            border.color: root.c.border
        }
        contentItem: ColumnLayout {
            spacing: UiMetrics.space3
            Text {
                Layout.fillWidth: true
                color: root.c.fg
                font.pixelSize: UiMetrics.fontL
                font.weight: Font.DemiBold
                text: root.trText("安装离线 Python 依赖？")
            }
            Text {
                Layout.fillWidth: true
                color: root.c.muted
                font.pixelSize: UiMetrics.fontS
                wrapMode: Text.Wrap
                text: root.trText("将从插件自带的 %1 个 wheel 安装 %2，并创建仅供该插件使用的私有环境。安装器不会访问网络。")
                    .arg(Number(dependencyConfirm.plan.wheelCount || 0))
                    .arg((dependencyConfirm.plan.packages || []).join(", "))
            }
            Text {
                Layout.fillWidth: true
                color: root.c.warning
                font.pixelSize: UiMetrics.fontTiny
                wrapMode: Text.Wrap
                text: root.trText("只有在你信任该插件及其 wheel 文件时才继续。")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: UiMetrics.space2
                UiButton {
                    variant: "ghost"
                    text: root.trText("取消")
                    onClicked: dependencyConfirm.close()
                }
                UiButton {
                    variant: "primary"
                    text: root.trText("确认安装")
                    onClicked: {
                        const id = dependencyConfirm.pluginId
                        dependencyConfirm.close()
                        settingsVM.installPluginDependencies(id)
                        detailPanel.refresh()
                    }
                }
            }
        }

        function ask(id, dependencyPlan) {
            pluginId = id
            plan = dependencyPlan || ({})
            open()
        }
    }

    Popup {
        id: packageInstallConfirm
        property var info: ({})
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.min(620, root.width - UiMetrics.space6 * 2)
        x: Math.max(0, (root.width - width) / 2)
        y: Math.max(0, (root.height - height) / 2)
        padding: UiMetrics.space5
        palette.window: root.c.bg
        palette.windowText: root.c.fg
        palette.base: root.c.bg
        palette.text: root.c.fg
        palette.button: root.c.panel
        palette.buttonText: root.c.fg
        palette.highlight: root.c.accent
        palette.highlightedText: root.c.onAccent
        palette.mid: root.c.muted
        background: Rectangle {
            color: root.c.panel
            radius: UiMetrics.radiusL
            border.width: 1
            border.color: root.c.border
        }
        contentItem: ColumnLayout {
            spacing: UiMetrics.space3
            Text {
                Layout.fillWidth: true
                color: root.c.fg
                font.pixelSize: UiMetrics.fontL
                font.weight: Font.DemiBold
                text: packageInstallConfirm.info.isUpdate === true
                    ? root.trText("更新插件包？") : root.trText("安装插件包？")
            }
            Text {
                Layout.fillWidth: true
                color: root.c.fg
                font.pixelSize: UiMetrics.fontM
                wrapMode: Text.Wrap
                text: root.trText("%1（%2） · v%3 · %4")
                    .arg(String(packageInstallConfirm.info.name || packageInstallConfirm.info.id || ""))
                    .arg(String(packageInstallConfirm.info.id || ""))
                    .arg(String(packageInstallConfirm.info.version || ""))
                    .arg(String(packageInstallConfirm.info.runtime || ""))
            }
            Text {
                Layout.fillWidth: true
                visible: packageInstallConfirm.info.isUpdate === true
                color: root.c.muted
                font.pixelSize: UiMetrics.fontS
                text: root.trText("当前版本：%1").arg(
                    String(packageInstallConfirm.info.currentVersion || root.trText("无")))
            }
            Text {
                Layout.fillWidth: true
                color: root.c.muted
                font.pixelSize: UiMetrics.fontS
                wrapMode: Text.Wrap
                text: root.trText("发布者：%1").arg(
                    String(packageInstallConfirm.info.publisher || root.trText("未声明")))
            }
            Text {
                Layout.fillWidth: true
                color: root.c.muted
                font.pixelSize: UiMetrics.fontTiny
                wrapMode: Text.WrapAnywhere
                text: root.trText("包 SHA-256：%1").arg(
                    String(packageInstallConfirm.info.packageHash || ""))
            }
            Text {
                Layout.fillWidth: true
                color: root.c.fg
                font.pixelSize: UiMetrics.fontS
                wrapMode: Text.Wrap
                text: root.trText("声明权限：%1").arg(root.permissionListText(
                    packageInstallConfirm.info.declaredPermissions))
            }
            Text {
                Layout.fillWidth: true
                color: root.c.fg
                font.pixelSize: UiMetrics.fontS
                wrapMode: Text.Wrap
                text: root.trText("已授予权限：%1").arg(root.permissionListText(
                    packageInstallConfirm.info.grantedPermissions))
            }
            Text {
                Layout.fillWidth: true
                color: packageInstallConfirm.info.signatureStatus === "verified"
                    ? root.c.success : root.c.warning
                font.pixelSize: UiMetrics.fontTiny
                wrapMode: Text.WrapAnywhere
                text: packageInstallConfirm.info.signatureStatus === "verified"
                    ? root.trText("发布者签名有效 · 密钥：%1").arg(String(
                        packageInstallConfirm.info.signerKeyId || ""))
                    : root.trText("未签名包：无法确认发布者身份。原生插件与宿主同进程运行，请只安装可信来源的包。")
            }
            Text {
                Layout.fillWidth: true
                visible: packageInstallConfirm.info.signatureStatus === "verified"
                color: root.c.muted
                font.pixelSize: UiMetrics.fontTiny
                wrapMode: Text.Wrap
                text: root.trText("签名证明内容由该密钥签发，但不代表玄枢已经信任此发布者。请核对来源和密钥标识。")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: UiMetrics.space2
                UiButton {
                    variant: "ghost"
                    text: root.trText("取消")
                    onClicked: packageInstallConfirm.close()
                }
                UiButton {
                    variant: "primary"
                    text: packageInstallConfirm.info.isUpdate === true
                        ? root.trText("确认更新") : root.trText("确认安装")
                    onClicked: {
                        const id = String(packageInstallConfirm.info.id || "")
                        const result = settingsVM.installPluginPackage(
                            String(packageInstallConfirm.info.path || ""))
                        packageInstallConfirm.close()
                        if (result.ok === true) {
                            settingsVM.clearPluginUpdates()
                            pluginList.currentId = id
                            pluginList.ensureSelection()
                            detailPanel.refresh()
                        }
                    }
                }
            }
        }

        function ask(packageInfo) {
            info = packageInfo || ({})
            open()
        }
    }

    Popup {
        id: packageActionConfirm
        property string operation: ""
        property var info: ({})
        property bool deleteUserData: false
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.min(540, root.width - UiMetrics.space6 * 2)
        x: Math.max(0, (root.width - width) / 2)
        y: Math.max(0, (root.height - height) / 2)
        padding: UiMetrics.space5
        palette.window: root.c.bg
        palette.windowText: root.c.fg
        palette.base: root.c.bg
        palette.text: root.c.fg
        palette.button: root.c.panel
        palette.buttonText: root.c.fg
        palette.highlight: root.c.accent
        palette.highlightedText: root.c.onAccent
        palette.mid: root.c.muted
        background: Rectangle {
            color: root.c.panel
            radius: UiMetrics.radiusL
            border.width: 1
            border.color: root.c.border
        }
        contentItem: ColumnLayout {
            spacing: UiMetrics.space3
            Text {
                Layout.fillWidth: true
                color: root.c.fg
                font.pixelSize: UiMetrics.fontL
                font.weight: Font.DemiBold
                text: packageActionConfirm.operation === "rollback"
                    ? root.trText("回滚插件包？") : root.trText("卸载插件包？")
            }
            Text {
                Layout.fillWidth: true
                color: root.c.muted
                font.pixelSize: UiMetrics.fontS
                wrapMode: Text.Wrap
                text: packageActionConfirm.operation === "rollback"
                    ? root.trText("将 %1 从 v%2 切换回 v%3。运行中的插件会安全停止并重新加载。")
                        .arg(String(packageActionConfirm.info.id || ""))
                        .arg(String(packageActionConfirm.info.version || ""))
                        .arg(String(packageActionConfirm.info.previousVersion || ""))
                    : root.trText("将移除 %1 的全部受管版本。若存在同 id 的内置插件，将自动恢复使用内置版本。")
                        .arg(String(packageActionConfirm.info.id || ""))
            }
            UiSwitch {
                visible: packageActionConfirm.operation === "uninstall"
                checked: packageActionConfirm.deleteUserData
                text: root.trText("同时删除插件用户数据")
                onToggled: packageActionConfirm.deleteUserData = checked
            }
            Text {
                Layout.fillWidth: true
                visible: packageActionConfirm.operation === "uninstall"
                         && packageActionConfirm.deleteUserData
                color: root.c.warning
                font.pixelSize: UiMetrics.fontTiny
                wrapMode: Text.Wrap
                text: root.trText("插件配置、私有环境和运行数据也会删除，此操作不可撤销。")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: UiMetrics.space2
                UiButton {
                    variant: "ghost"
                    text: root.trText("取消")
                    onClicked: packageActionConfirm.close()
                }
                UiButton {
                    variant: "primary"
                    text: packageActionConfirm.operation === "rollback"
                        ? root.trText("确认回滚") : root.trText("确认卸载")
                    onClicked: {
                        const id = String(packageActionConfirm.info.id || "")
                        const result = packageActionConfirm.operation === "rollback"
                            ? settingsVM.rollbackPluginPackage(id)
                            : settingsVM.uninstallPluginPackage(id,
                                  packageActionConfirm.deleteUserData)
                        packageActionConfirm.close()
                        if (result.ok === true) {
                            pluginList.ensureSelection()
                            detailPanel.refresh()
                        }
                    }
                }
            }
        }

        function askRollback(packageInfo) {
            operation = "rollback"
            info = packageInfo || ({})
            deleteUserData = false
            open()
        }
        function askUninstall(packageInfo) {
            operation = "uninstall"
            info = packageInfo || ({})
            deleteUserData = false
            open()
        }
    }

    Popup {
        id: updateSourcesPopup
        property string originalSources: ""
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        width: Math.min(640, root.width - UiMetrics.space6 * 2)
        x: Math.max(0, (root.width - width) / 2)
        y: Math.max(0, (root.height - height) / 2)
        padding: UiMetrics.space5
        palette.window: root.c.bg
        palette.windowText: root.c.fg
        palette.base: root.c.bg
        palette.text: root.c.fg
        palette.button: root.c.panel
        palette.buttonText: root.c.fg
        palette.highlight: root.c.accent
        palette.highlightedText: root.c.onAccent
        palette.mid: root.c.muted
        background: Rectangle {
            color: root.c.panel
            radius: UiMetrics.radiusL
            border.width: 1
            border.color: root.c.border
        }
        contentItem: ColumnLayout {
            spacing: UiMetrics.space3
            Text {
                Layout.fillWidth: true
                color: root.c.fg
                font.pixelSize: UiMetrics.fontL
                font.weight: Font.DemiBold
                text: root.trText("插件更新源")
            }
            Text {
                Layout.fillWidth: true
                color: root.c.muted
                font.pixelSize: UiMetrics.fontS
                wrapMode: Text.Wrap
                text: root.trText("每行一个 HTTPS 或本机 file URL。检查更新只读取索引，不会自动下载或安装插件。")
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                radius: UiMetrics.radiusS
                color: root.c.bg
                border.width: 1
                border.color: updateSourcesEditor.activeFocus
                    ? root.c.accent : root.c.border
                TextArea {
                    id: updateSourcesEditor
                    anchors.fill: parent
                    anchors.margins: UiMetrics.space2
                    color: root.c.fg
                    placeholderTextColor: root.c.muted
                    selectionColor: root.c.accent
                    selectedTextColor: root.c.onAccent
                    font.pixelSize: UiMetrics.fontS
                    wrapMode: TextEdit.WrapAnywhere
                    placeholderText: "https://example.com/xuanhub/index.json"
                    background: null
                    onTextChanged: if (activeFocus) settingsVM.updateSources = text
                }
            }
            Text {
                Layout.fillWidth: true
                color: root.c.warning
                font.pixelSize: UiMetrics.fontTiny
                wrapMode: Text.Wrap
                text: root.trText("更新源不是信任根；下载后仍会校验大小、SHA-256、Manifest、兼容性、权限和发布者签名。")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: UiMetrics.space2
                UiButton {
                    variant: "ghost"
                    text: root.trText("取消")
                    onClicked: {
                        settingsVM.updateSources = updateSourcesPopup.originalSources
                        updateSourcesPopup.close()
                    }
                }
                UiButton {
                    variant: "primary"
                    text: root.trText("使用这些更新源")
                    onClicked: updateSourcesPopup.close()
                }
            }
        }

        function ask() {
            originalSources = settingsVM.updateSources
            updateSourcesEditor.text = settingsVM.updateSources
            open()
            updateSourcesEditor.forceActiveFocus()
        }
    }

    Popup {
        id: updateResultsPopup
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.min(680, root.width - UiMetrics.space6 * 2)
        height: Math.min(560, root.height - UiMetrics.space6 * 2)
        x: Math.max(0, (root.width - width) / 2)
        y: Math.max(0, (root.height - height) / 2)
        padding: UiMetrics.space5
        palette.window: root.c.bg
        palette.windowText: root.c.fg
        palette.base: root.c.bg
        palette.text: root.c.fg
        palette.button: root.c.panel
        palette.buttonText: root.c.fg
        palette.highlight: root.c.accent
        palette.highlightedText: root.c.onAccent
        palette.mid: root.c.muted
        background: Rectangle {
            color: root.c.panel
            radius: UiMetrics.radiusL
            border.width: 1
            border.color: root.c.border
        }
        contentItem: ColumnLayout {
            spacing: UiMetrics.space3
            Text {
                Layout.fillWidth: true
                color: root.c.fg
                font.pixelSize: UiMetrics.fontL
                font.weight: Font.DemiBold
                text: root.trText("可用插件更新")
            }
            Text {
                Layout.fillWidth: true
                color: root.c.muted
                font.pixelSize: UiMetrics.fontS
                wrapMode: Text.Wrap
                text: root.trText("选择后才会下载并验证插件包；验证通过后仍需再次确认安装。")
            }
            Text {
                Layout.fillWidth: true
                visible: settingsVM.availablePluginUpdates.length === 0
                color: root.c.muted
                font.pixelSize: UiMetrics.fontM
                text: root.trText("当前没有可用更新")
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ColumnLayout {
                    width: updateResultsPopup.availableWidth
                    spacing: UiMetrics.space2
                    Repeater {
                        model: settingsVM.availablePluginUpdates
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 76
                            radius: UiMetrics.radiusS
                            color: root.c.bg
                            border.width: 1
                            border.color: root.c.border
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: UiMetrics.space3
                                spacing: UiMetrics.space3
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        color: root.c.fg
                                        font.pixelSize: UiMetrics.fontM
                                        font.weight: Font.DemiBold
                                        text: modelData.id
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        color: root.c.muted
                                        font.pixelSize: UiMetrics.fontTiny
                                        text: root.trText("版本 %1 → %2 · 来源 %3")
                                            .arg(modelData.currentVersion)
                                            .arg(modelData.version)
                                            .arg(modelData.sourceId)
                                        elide: Text.ElideRight
                                    }
                                }
                                UiButton {
                                    enabled: !settingsVM.pluginUpdateDownloading
                                    variant: "primary"
                                    text: settingsVM.pluginUpdateDownloading
                                        ? root.trText("正在下载…")
                                        : root.trText("下载并验证")
                                    onClicked: settingsVM.downloadPluginUpdate(
                                        modelData.id, modelData.version)
                                }
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: settingsVM.pluginUpdateErrors.length > 0
                spacing: UiMetrics.space1
                Text {
                    color: root.c.warning
                    font.pixelSize: UiMetrics.fontS
                    font.weight: Font.DemiBold
                    text: root.trText("部分更新源未通过检查")
                }
                Text {
                    Layout.fillWidth: true
                    color: root.c.muted
                    font.pixelSize: UiMetrics.fontTiny
                    wrapMode: Text.WrapAnywhere
                    text: settingsVM.pluginUpdateErrors.join("\n")
                }
            }
            UiButton {
                Layout.alignment: Qt.AlignRight
                variant: "ghost"
                text: root.trText("关闭")
                onClicked: updateResultsPopup.close()
            }
        }
    }

    Connections {
        target: settingsVM
        function onPluginUpdateCheckFinished() { updateResultsPopup.open() }
        function onPluginUpdatePackageReady(packageInfo) {
            updateResultsPopup.close()
            packageInstallConfirm.ask(packageInfo)
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

    // ThemeManager 的色板经 QVariantMap 传入时是颜色字符串。赋给 color 属性后才会
    // 转成带 r/g/b 分量的 QColor；不要直接访问 c.panel.r，否则会得到 undefined，
    // 半透明层会退化成黑色（浅色主题的导航与底栏尤为明显）。
    readonly property color bgColor: c.bg
    readonly property color panelColor: c.panel
    readonly property color fgColor: c.fg
    readonly property color accentColor: c.accent
    readonly property color dangerColor: c.danger
    function translucent(color, opacity) {
        return Qt.rgba(color.r, color.g, color.b, opacity)
    }

    // 页名/图标表：导航、标题栏副标题、StackLayout 共用一份索引
    readonly property var pageNames: [
        qsTranslate("xuanhub.settings", "外观"),
        qsTranslate("xuanhub.settings", "插件"),
        qsTranslate("xuanhub.settings", "通用")
    ]
    readonly property var pageIcons: ["◐", "⌘", "⚙"]
    function positionLabel(position) {
        if (position === "left") return trText("左侧区域")
        if (position === "center") return trText("中间区域")
        return position === "right" ? trText("右侧区域") : trText("未识别区域")
    }
    function groupLabel(groupKey) {
        const separator = groupKey.indexOf("-")
        const enabled = groupKey.substring(0, separator) === "enabled"
        const position = separator >= 0 ? groupKey.substring(separator + 1) : groupKey
        return (enabled ? trText("已启用") : trText("未启用"))
                + " · " + positionLabel(position)
    }
    function healthLabel(state) {
        if (state === "starting") return trText("正在启动")
        if (state === "running") return trText("运行正常")
        if (state === "degraded") return trText("运行异常")
        if (state === "restarting") return trText("正在重启")
        if (state === "failed") return trText("启动失败")
        if (state === "circuitOpen") return trText("已熔断")
        if (state === "stopping") return trText("正在停止")
        if (state === "stopped") return trText("已停止")
        if (state === "disabled") return trText("已停用")
        return trText("等待启动")
    }
    function healthTime(value) {
        if (value === undefined || value === null || value === "") return trText("暂无")
        const date = new Date(value)
        if (isNaN(date.getTime())) return trText("暂无")
        return date.toLocaleString(Qt.locale(i18n.activeLocale), Locale.ShortFormat)
    }
    function permissionListText(values) {
        return values && values.length > 0 ? values.join(", ") : trText("无")
    }
    // HostServices.navigation.openSettings() 会调用此入口。按稳定 id 选中插件，
    // 不依赖排序后的行号，避免启用状态变化后打开到错误详情页。
    function selectPlugin(pluginId) {
        pageStack.currentIndex = 1
        if (pluginId && settingsVM.pluginModel.rowForId(pluginId) >= 0)
            pluginList.currentId = pluginId
        else
            pluginList.ensureSelection()
    }

    // 插件设置草稿读取：config 无该键时用 schema default 兜底（类型化表单与自绘页共用）
    function draftValue(key, def) {
        const v = settingsVM.pluginProxy.value(key)
        return (v === undefined || v === null) ? def : v
    }

    // 条件格式：{ "key": "mode", "equals": "advanced" }；另支持
    // notEquals / in / truthy。显式引用 revision，让其他字段改动后立即重算。
    function schemaConditionMatches(condition) {
        const revision = settingsVM.pluginProxy.revision
        if (!condition || !condition.key) return true
        const current = settingsVM.pluginProxy.value(String(condition.key))
        if (condition.equals !== undefined) return current === condition.equals
        if (condition.notEquals !== undefined) return current !== condition.notEquals
        if (condition.in !== undefined && Array.isArray(condition.in))
            return condition.in.indexOf(current) >= 0
        if (condition.truthy !== undefined)
            return Boolean(current) === Boolean(condition.truthy)
        return true
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

        // ── 自绘标题栏（无边框窗口）：产品识别 + 当前页 + 保存提示 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: UiMetrics.space5
                anchors.rightMargin: UiMetrics.space3
                spacing: UiMetrics.space3

                Image {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    source: "qrc:/qt/qml/XuanHub/resources/xuanhub-icon-256.png"
                    sourceSize: Qt.size(64, 64)
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }
                Text {
                    text: root.trText("玄枢设置 XuanHub Settings")
                    color: root.c.fg
                    font.pixelSize: UiMetrics.fontL
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: Qt.application.version.length > 0
                    text: "v" + Qt.application.version
                    color: root.c.muted
                    font.pixelSize: UiMetrics.fontS
                }
                Rectangle {
                    radius: 10
                    color: root.translucent(root.accentColor, 0.12)
                    implicitWidth: pageName.implicitWidth + 16
                    implicitHeight: 24
                    Text {
                        id: pageName
                        anchors.centerIn: parent
                        text: root.pageNames[pageStack.currentIndex]
                        color: root.c.accent
                        font.pixelSize: UiMetrics.fontS
                        font.weight: Font.DemiBold
                    }
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: root.invalidFields > 0 ? root.trText("请修正输入错误")
                                                 : root.trText("更改将在保存后生效")
                    color: root.invalidFields > 0 ? root.c.danger : root.c.muted
                    font.pixelSize: UiMetrics.fontS
                }

                Item {
                    id: closeBtn
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    Rectangle {
                        anchors.fill: parent
                        radius: UiMetrics.radiusS
                        color: closeMa.containsMouse
                               ? root.translucent(root.dangerColor, 0.14)
                               : "transparent"
                        Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        color: closeMa.containsMouse ? root.c.danger : root.c.fg
                        font.pixelSize: 20
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
                anchors.rightMargin: 64
                acceptedButtons: Qt.LeftButton
                onPressed: (mouse) => {
                    if (mouse.button === Qt.LeftButton)
                        Window.window.startSystemMove()   // QWindow::startSystemMove（Win32 SC_MOVE）
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                readonly property color bord: root.c.border
                color: Qt.rgba(bord.r, bord.g, bord.b, 0.5)
            }
        }

        // ── 主体：Fluent 左侧导航 + 页面区 ──
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 左侧导航：大触控区、图标与文字；选中状态由每项自身绘制，避免硬编码 y。
            Rectangle {
                Layout.preferredWidth: 188
                Layout.fillHeight: true
                // 使用不透明的主题面板色，既保证浅色主题的层次，也避免透明窗口
                // 与桌面合成时污染导航颜色。
                color: root.panelColor

                ColumnLayout {
                    id: navCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: UiMetrics.space3
                    spacing: UiMetrics.space1

                    Text {
                        text: root.trText("设置")
                        color: root.c.muted
                        font.pixelSize: UiMetrics.fontTiny
                        font.weight: Font.DemiBold
                        Layout.leftMargin: UiMetrics.space2
                        Layout.topMargin: UiMetrics.space1
                        Layout.bottomMargin: UiMetrics.space1
                    }

                    Repeater {
                        model: root.pageNames
                        delegate: Rectangle {
                            id: navItem
                            Layout.fillWidth: true
                            Layout.preferredHeight: 48
                            radius: UiMetrics.radiusS
                            property bool selected: pageStack.currentIndex === index
                            readonly property color acc: root.c.accent
                            readonly property color fg: root.c.fg
                            color: selected ? Qt.rgba(acc.r, acc.g, acc.b, 0.14)
                                 : navMa.containsMouse ? Qt.rgba(fg.r, fg.g, fg.b, 0.06)
                                 : "transparent"
                            Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }

                            Rectangle {
                                visible: navItem.selected
                                anchors.left: parent.left
                                anchors.leftMargin: 0
                                anchors.verticalCenter: parent.verticalCenter
                                width: 3
                                height: 24
                                radius: 1.5
                                color: navItem.acc
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: UiMetrics.space3
                                anchors.rightMargin: UiMetrics.space3
                                spacing: UiMetrics.space2
                                Text {
                                    text: root.pageIcons[index]
                                    color: navItem.selected ? navItem.acc : root.c.muted
                                    font.pixelSize: 20
                                    Layout.preferredWidth: 24
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Text {
                                    text: modelData
                                    color: navItem.fg
                                    font.pixelSize: UiMetrics.fontM
                                    font.weight: navItem.selected ? Font.DemiBold : Font.Normal
                                    Layout.fillWidth: true
                                }
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
                            x: UiMetrics.space5
                            y: UiMetrics.space5
                            width: appearanceScroll.availableWidth - UiMetrics.space5 * 2
                            spacing: UiMetrics.space4

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: UiMetrics.space1
                                Text {
                                    text: root.trText("外观")
                                    color: root.c.fg
                                    font.pixelSize: UiMetrics.fontXL
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: root.trText("调整状态栏主题、尺寸和停靠方式")
                                    color: root.c.muted
                                    font.pixelSize: UiMetrics.fontM
                                }
                            }

                            UiCard {
                                Layout.fillWidth: true
                                title: root.trText("主题")
                                description: root.trText("选择主题包和明暗模式；保存后立即热重载")

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: appearanceScroll.availableWidth > 680 ? 2 : 1
                                    columnSpacing: UiMetrics.space4
                                    rowSpacing: UiMetrics.space3

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: UiMetrics.space1
                                        Text {
                                            text: root.trText("主题包")
                                            color: root.c.fg
                                            font.pixelSize: UiMetrics.fontS
                                            font.weight: Font.DemiBold
                                        }
                                        Text {
                                            text: root.trText("决定色板与整体视觉风格")
                                            color: root.c.muted
                                            font.pixelSize: UiMetrics.fontTiny
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
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: UiMetrics.space1
                                        Text {
                                            text: root.trText("模式")
                                            color: root.c.fg
                                            font.pixelSize: UiMetrics.fontS
                                            font.weight: Font.DemiBold
                                        }
                                        Text {
                                            text: root.trText("浅色和深色模式均可跟随主题包")
                                            color: root.c.muted
                                            font.pixelSize: UiMetrics.fontTiny
                                        }
                                        UiComboBox {
                                            model: [
                                                { value: "dark", label: root.trText("深色") },
                                                { value: "light", label: root.trText("浅色") }
                                            ]
                                            textRole: "label"
                                            valueRole: "value"
                                            currentIndex: settingsVM.theme === "light" ? 1 : 0
                                            onActivated: settingsVM.theme = currentValue
                                        }
                                    }
                                }
                            }

                            UiCard {
                                Layout.fillWidth: true
                                title: root.trText("状态栏")
                                description: root.trText("尺寸、停靠边缘与系统集成方式")

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: appearanceScroll.availableWidth > 680 ? 2 : 1
                                    columnSpacing: UiMetrics.space4
                                    rowSpacing: UiMetrics.space3

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: UiMetrics.space1
                                        Text { text: root.trText("高度（像素）"); color: root.c.fg; font.pixelSize: UiMetrics.fontS; font.weight: Font.DemiBold }
                                        Text { text: root.trText("状态栏的可视高度"); color: root.c.muted; font.pixelSize: UiMetrics.fontTiny }
                                        UiSpinBox { from: 24; to: 200; value: settingsVM.barHeight; onValueModified: settingsVM.barHeight = value }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: UiMetrics.space1
                                        Text { text: root.trText("停靠边缘"); color: root.c.fg; font.pixelSize: UiMetrics.fontS; font.weight: Font.DemiBold }
                                        Text { text: root.trText("选择显示在屏幕顶部或底部"); color: root.c.muted; font.pixelSize: UiMetrics.fontTiny }
                                        UiComboBox {
                                            model: [
                                                { value: "top", label: root.trText("顶部") },
                                                { value: "bottom", label: root.trText("底部") }
                                            ]
                                            textRole: "label"; valueRole: "value"
                                            currentIndex: settingsVM.barEdge === "bottom" ? 1 : 0
                                            onActivated: settingsVM.barEdge = currentValue
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: UiMetrics.space1
                                        Text { text: root.trText("工作模式"); color: root.c.fg; font.pixelSize: UiMetrics.fontS; font.weight: Font.DemiBold }
                                        Text { text: root.trText("appbar 占用工作区；taskbar 嵌入任务栏"); color: root.c.muted; font.pixelSize: UiMetrics.fontTiny }
                                        UiComboBox {
                                            model: [
                                                { value: "appbar", label: "AppBar" },
                                                { value: "taskbar", label: root.trText("任务栏") }
                                            ]
                                            textRole: "label"
                                            valueRole: "value"
                                            currentIndex: settingsVM.barMode === "taskbar" ? 1 : 0
                                            onActivated: settingsVM.barMode = currentValue
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: UiMetrics.space1
                                        Text { text: root.trText("透明度 (%)"); color: root.c.fg; font.pixelSize: UiMetrics.fontS; font.weight: Font.DemiBold }
                                        Text { text: root.trText("最低 20%，避免状态栏不可见"); color: root.c.muted; font.pixelSize: UiMetrics.fontTiny }
                                        UiSpinBox {
                                            from: 20; to: 100; stepSize: 5
                                            value: settingsVM.barOpacity
                                            onValueModified: settingsVM.barOpacity = value
                                        }
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
                            id: pluginPane
                            Layout.preferredWidth: 250
                            Layout.fillHeight: true
                            color: root.translucent(root.panelColor, 0.72)
                            radius: UiMetrics.radiusM
                            border.width: 1
                            readonly property color bord: root.c.border
                            border.color: Qt.rgba(bord.r, bord.g, bord.b, 0.28)
                            clip: true   // 行圆角不溢出面板圆角
                            property string filterText: pluginSearch.text.trim().toLowerCase()

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: UiMetrics.space3
                                spacing: UiMetrics.space2

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: root.trText("插件")
                                        color: root.c.fg
                                        font.pixelSize: UiMetrics.fontL
                                        font.weight: Font.DemiBold
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: settingsVM.pluginModel.count + " " + root.trText("个插件")
                                        color: root.c.muted
                                        font.pixelSize: UiMetrics.fontTiny
                                    }
                                }
                                UiTextField {
                                    id: pluginSearch
                                    Layout.fillWidth: true
                                    placeholderText: root.trText("搜索插件…")
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: UiMetrics.space1
                                    UiButton {
                                        Layout.fillWidth: true
                                        compact: true
                                        variant: "outline"
                                        text: root.trText("安装…")
                                        ToolTip.visible: hovered
                                        ToolTip.text: root.trText("安装插件包…")
                                        onClicked: {
                                            const path = settingsCtl.choosePluginPackage()
                                            if (!path || path.length === 0) return
                                            const info = settingsVM.inspectPluginPackage(path)
                                            if (info.ok === true) packageInstallConfirm.ask(info)
                                        }
                                    }
                                    UiButton {
                                        Layout.fillWidth: true
                                        compact: true
                                        variant: "outline"
                                        text: root.trText("更新源")
                                        ToolTip.visible: hovered
                                        ToolTip.text: root.trText("更新源…")
                                        onClicked: updateSourcesPopup.ask()
                                    }
                                    UiButton {
                                        Layout.fillWidth: true
                                        compact: true
                                        enabled: !settingsVM.pluginUpdateChecking
                                                 && !settingsVM.pluginUpdateDownloading
                                        variant: "outline"
                                        text: settingsVM.pluginUpdateChecking
                                            ? root.trText("检查中…")
                                            : root.trText("检查")
                                        ToolTip.visible: hovered
                                        ToolTip.text: root.trText("检查更新")
                                        onClicked: settingsVM.checkPluginUpdates()
                                    }
                                }

                                ListView {
                                    id: pluginList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    spacing: UiMetrics.space1
                                    // SettingsPluginSortModel：已启用插件总在前，再按左/中/右
                                    // 区域与由上移/下移维护的顺序排列。组合分组键令区域标题在
                                    // 启用状态切换处也保持准确，不会把两段同名区域误合并。
                                    model: settingsVM.pluginModel
                                    section.property: "groupKey"
                                    section.criteria: ViewSection.FullString

                                    function matchesRow(row) {
                                        const name = String(model.roleValue(row, "name")
                                                                  || model.roleValue(row, "id")).toLowerCase()
                                        const id = String(model.roleValue(row, "id")).toLowerCase()
                                        return pluginPane.filterText.length === 0
                                            || name.indexOf(pluginPane.filterText) >= 0
                                            || id.indexOf(pluginPane.filterText) >= 0
                                    }
                                    function sectionHasMatch(groupKey) {
                                        for (let row = 0; row < model.count(); ++row) {
                                            if (model.roleValue(row, "groupKey") === groupKey
                                                    && matchesRow(row)) return true
                                        }
                                        return false
                                    }
                                    section.delegate: Item {
                                        width: pluginList.width
                                        height: pluginList.sectionHasMatch(section) ? 28 : 0
                                        visible: height > 0
                                        Text {
                                            id: sectionName
                                            anchors.left: parent.left
                                            anchors.leftMargin: UiMetrics.space2
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: root.groupLabel(section)
                                            color: root.c.muted
                                            font.pixelSize: UiMetrics.fontTiny
                                            font.weight: Font.DemiBold
                                        }
                                        Rectangle {
                                            anchors.left: sectionName.right
                                            anchors.leftMargin: UiMetrics.space2
                                            anchors.right: parent.right
                                            anchors.rightMargin: UiMetrics.space2
                                            anchors.verticalCenter: parent.verticalCenter
                                            height: 1
                                            color: root.translucent(root.fgColor, 0.14)
                                        }
                                    }

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
                                    property bool matches: pluginList.matchesRow(index)
                                    height: matches ? 56 : 0
                                    visible: matches
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
                                        height: 26
                                        radius: 1.5
                                        color: pluginRow.acc
                                    }
                                    Rectangle {   // 启用状态点
                                        id: dot
                                        anchors.left: parent.left
                                        anchors.leftMargin: UiMetrics.space3
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 9; height: 9; radius: 4.5
                                        color: model.enabled ? root.c.accent : root.c.muted
                                    }
                                    ColumnLayout {
                                        anchors.left: dot.right
                                        anchors.leftMargin: 8
                                        anchors.right: parent.right
                                        anchors.rightMargin: UiMetrics.space2
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 0
                                        Text {
                                            color: root.c.fg
                                            font.pixelSize: UiMetrics.fontM
                                            font.weight: pluginRow.selected ? Font.DemiBold : Font.Normal
                                            text: model.name ? model.name : model.id
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            color: root.c.muted
                                            font.pixelSize: UiMetrics.fontTiny
                                            text: model.id + " · " + (model.kind === "python" ? "Python" : "C++")
                                                  + (model.discovered ? "" : " · " + root.trText("磁盘缺失"))
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
                        }

                        // ── 右栏：选中插件的设置 ──
                        Rectangle {
                            id: detailPanel
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: root.translucent(root.panelColor, 0.56)
                            radius: UiMetrics.radiusM
                            border.width: 1
                            readonly property color bord: root.c.border
                            border.color: Qt.rgba(bord.r, bord.g, bord.b, 0.28)

                            // 行数据快照：模型的读接口非响应式，统一在模型信号/选中变化时
                            // 重取；面板控件只绑这份快照，避免与 C++ 直读打架
                            property int currentRow: -1
                            property var rowData: ({})

                            // 重入守卫：自绘设置页注入后写默认值 → setConfigValue →
                            // dataChanged → 本函数被重入 → rowData 重赋 → customPage.source
                            // 重评估 → 页面重载又写值（日志刷 binding loop 噪音）。重入调用
                            // 只记标记，收尾后经 callLater 顺延一次（跳出当前调用栈断链）
                            property bool _refreshing: false
                            property bool _pendingRefresh: false

                            function refresh() {
                                if (_refreshing) { _pendingRefresh = true; return }
                                _refreshing = true
                                const m = settingsVM.pluginModel
                                currentRow = m.rowForId(pluginList.currentId)
                                if (currentRow < 0) {
                                    rowData = ({})
                                } else {
                                    rowData = {
                                        "name": m.roleValue(currentRow, "name"),
                                        "version": m.roleValue(currentRow, "version"),
                                        "author": m.roleValue(currentRow, "author"),
                                        "kind": m.roleValue(currentRow, "kind"),
                                        "enabled": m.roleValue(currentRow, "enabled"),
                                        "position": m.roleValue(currentRow, "position"),
                                        "width": m.roleValue(currentRow, "width"),
                                        "discovered": m.roleValue(currentRow, "discovered"),
                                        "schema": m.roleValue(currentRow, "schema"),
                                        "settingsQml": m.roleValue(currentRow, "settingsQml"),
                                        "apiVersion": m.roleValue(currentRow, "apiVersion"),
                                        "capabilities": m.roleValue(currentRow, "capabilities"),
                                        "declaredPermissions": m.roleValue(currentRow, "declaredPermissions"),
                                        "grantedPermissions": m.roleValue(currentRow, "grantedPermissions"),
                                        "trustLevel": m.roleValue(currentRow, "trustLevel"),
                                        "configVersion": m.roleValue(currentRow, "configVersion"),
                                        "targetConfigVersion": m.roleValue(currentRow, "targetConfigVersion"),
                                        "migrationAvailable": m.roleValue(currentRow, "migrationAvailable"),
                                        "migrationError": m.roleValue(currentRow, "migrationError"),
                                        "layoutPreference": m.roleValue(currentRow, "layoutPreference"),
                                        "health": settingsVM.pluginHealth(pluginList.currentId),
                                        "dependencyPlan": settingsVM.pluginDependencyPlan(pluginList.currentId),
                                        "managedInfo": settingsVM.managedPluginInfo(pluginList.currentId)
                                    }
                                }
                                _refreshing = false
                                if (_pendingRefresh) {
                                    _pendingRefresh = false
                                    Qt.callLater(refresh)
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
                            Connections {
                                target: settingsVM
                                function onPluginHealthChanged(id) {
                                    if (id.length === 0 || id === pluginList.currentId)
                                        detailPanel.refresh()
                                }
                                function onPluginDependencyInstallFinished(id) {
                                    if (id === pluginList.currentId) detailPanel.refresh()
                                }
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
                                    text: root.trText("在左侧选择一个插件")
                                }
                            }

                            ScrollView {
                                id: detailScroll
                                anchors.fill: parent
                                anchors.margins: UiMetrics.space4
                                clip: true
                                visible: detailPanel.currentRow >= 0

                                ColumnLayout {
                                    width: detailScroll.availableWidth
                                    spacing: UiMetrics.space3

                                    // 头部：名称、类型徽章与启用状态
                                    RowLayout {
                                        Layout.fillWidth: true
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: UiMetrics.space1
                                            Text {
                                                color: root.c.fg
                                                font.pixelSize: UiMetrics.fontXL
                                                font.weight: Font.DemiBold
                                                text: {
                                                    const n = detailPanel.rowData.name
                                                    return (n && n.length > 0) ? n : pluginList.currentId
                                                }
                                            }
                                            RowLayout {
                                                spacing: UiMetrics.space2
                                                Rectangle {
                                                    radius: 10
                                                    color: root.translucent(root.accentColor, 0.14)
                                                    implicitWidth: typeTag.implicitWidth + 14
                                                    implicitHeight: 22
                                                    Text {
                                                        id: typeTag
                                                        anchors.centerIn: parent
                                                        text: detailPanel.rowData.kind === "python" ? "Python" : "C++"
                                                        color: root.c.accent
                                                        font.pixelSize: UiMetrics.fontTiny
                                                        font.weight: Font.DemiBold
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
                                                        if (d.discovered === false) s += " · " + root.trText("磁盘缺失")
                                                        return s
                                                    }
                                                }
                                            }
                                        }
                                        UiSwitch {
                                            text: checked ? root.trText("已启用") : root.trText("已停用")
                                            checked: detailPanel.rowData.enabled === true
                                            onToggled: settingsVM.pluginModel.setRoleValue(
                                                           detailPanel.currentRow, "enabled", checked)
                                        }
                                    }

                                    UiCard {
                                        Layout.fillWidth: true
                                        compact: true
                                        visible: detailPanel.rowData.migrationAvailable === true
                                                 || String(detailPanel.rowData.migrationError || "").length > 0
                                        title: root.trText("配置迁移")
                                        description: detailPanel.rowData.migrationAvailable === true
                                            ? root.trText("检测到插件配置版本 %1，可迁移到版本 %2。迁移只修改当前草稿，点击保存后才会原子写入。")
                                                .arg(Number(detailPanel.rowData.configVersion || 1))
                                                .arg(Number(detailPanel.rowData.targetConfigVersion || 1))
                                            : root.trText("插件配置迁移失败，当前插件已停用；旧配置保持不变。")

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: UiMetrics.space2
                                            Text {
                                                Layout.fillWidth: true
                                                visible: String(detailPanel.rowData.migrationError || "").length > 0
                                                color: root.c.danger
                                                font.pixelSize: UiMetrics.fontS
                                                wrapMode: Text.Wrap
                                                text: String(detailPanel.rowData.migrationError || "")
                                            }
                                            UiButton {
                                                visible: detailPanel.rowData.migrationAvailable === true
                                                         && String(detailPanel.rowData.migrationError || "").length === 0
                                                variant: "outline"
                                                text: root.trText("采用迁移结果")
                                                onClicked: settingsVM.acceptPluginMigration(pluginList.currentId)
                                            }
                                        }
                                    }

                                    UiCard {
                                        Layout.fillWidth: true
                                        compact: true
                                        visible: (detailPanel.rowData.managedInfo || {}).managed === true
                                        title: root.trText("插件包管理")
                                        description: root.trText("查看已安装版本并执行安全回滚或卸载")

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: UiMetrics.space2
                                            Text {
                                                Layout.fillWidth: true
                                                color: root.c.fg
                                                font.pixelSize: UiMetrics.fontS
                                                wrapMode: Text.Wrap
                                                text: root.trText("当前版本：%1").arg(String(
                                                    (detailPanel.rowData.managedInfo || {}).version || ""))
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                color: root.c.muted
                                                font.pixelSize: UiMetrics.fontTiny
                                                wrapMode: Text.Wrap
                                                text: root.trText("来源：%1 · 发布者：%2")
                                                    .arg(String((detailPanel.rowData.managedInfo || {}).sourceName
                                                        || root.trText("本地文件")))
                                                    .arg(String((detailPanel.rowData.managedInfo || {}).publisher
                                                        || root.trText("未声明")))
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                color: root.c.muted
                                                font.pixelSize: UiMetrics.fontTiny
                                                wrapMode: Text.WrapAnywhere
                                                text: root.trText("包 SHA-256：%1").arg(String(
                                                    (detailPanel.rowData.managedInfo || {}).packageHash || ""))
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                color: (detailPanel.rowData.managedInfo || {}).signatureStatus === "verified"
                                                    ? root.c.success : root.c.warning
                                                font.pixelSize: UiMetrics.fontTiny
                                                wrapMode: Text.WrapAnywhere
                                                text: (detailPanel.rowData.managedInfo || {}).signatureStatus === "verified"
                                                    ? root.trText("发布者签名有效 · 密钥：%1").arg(String(
                                                        (detailPanel.rowData.managedInfo || {}).signerKeyId || ""))
                                                    : root.trText("未签名 · 发布者身份未经验证")
                                            }
                                            RowLayout {
                                                spacing: UiMetrics.space2
                                                UiButton {
                                                    visible: (detailPanel.rowData.managedInfo || {}).rollbackAvailable === true
                                                    variant: "outline"
                                                    text: root.trText("回滚到 v%1").arg(String(
                                                        (detailPanel.rowData.managedInfo || {}).previousVersion || ""))
                                                    onClicked: packageActionConfirm.askRollback(
                                                        detailPanel.rowData.managedInfo)
                                                }
                                                UiButton {
                                                    variant: "outline"
                                                    text: root.trText("卸载插件包")
                                                    onClicked: packageActionConfirm.askUninstall(
                                                        detailPanel.rowData.managedInfo)
                                                }
                                            }
                                        }
                                    }

                                    // ── 启用后的设置区 ──
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 10
                                        visible: detailPanel.rowData.enabled === true

                                        UiCard {
                                            Layout.fillWidth: true
                                            compact: true
                                            title: root.trText("基础设置")
                                            description: root.trText("控制插件在状态栏中的位置、显示顺序与占用宽度")

                                            GridLayout {
                                                Layout.fillWidth: true
                                                columns: detailScroll.availableWidth > 720 ? 2 : 1
                                                columnSpacing: UiMetrics.space4
                                                rowSpacing: UiMetrics.space3

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: UiMetrics.space1
                                                    Text { text: root.trText("位置"); color: root.c.fg; font.pixelSize: UiMetrics.fontS; font.weight: Font.DemiBold }
                                                    Text { text: root.trText("显示在状态栏的哪一栏"); color: root.c.muted; font.pixelSize: UiMetrics.fontTiny }
                                                    UiComboBox {
                                                        model: [
                                                            { value: "left", label: root.trText("左侧") },
                                                            { value: "center", label: root.trText("中间") },
                                                            { value: "right", label: root.trText("右侧") }
                                                        ]
                                                        textRole: "label"
                                                        valueRole: "value"
                                                        currentIndex: Math.max(0, indexOfValue(detailPanel.rowData.position))
                                                        onActivated: settingsVM.pluginModel.setRoleValue(
                                                                         detailPanel.currentRow, "position", currentValue)
                                                    }
                                                }
                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: UiMetrics.space1
                                                    Text { text: root.trText("顺序"); color: root.c.fg; font.pixelSize: UiMetrics.fontS; font.weight: Font.DemiBold }
                                                    Text { text: root.trText("仅在当前区域内调整"); color: root.c.muted; font.pixelSize: UiMetrics.fontTiny }
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: UiMetrics.space2
                                                        UiButton {
                                                            variant: "outline"
                                                            text: "↑ " + root.trText("上移")
                                                            // 82 是中文下的最小触控宽度；英文等较长翻译按
                                                            // UiButton 的文本 + padding 自然宽度扩展，避免溢出。
                                                            Layout.preferredWidth: Math.max(82, implicitWidth)
                                                            enabled: settingsVM.pluginModel.canMoveUp(
                                                                         detailPanel.currentRow)
                                                            onClicked: settingsVM.pluginModel.moveUp(
                                                                           detailPanel.currentRow)
                                                        }
                                                        UiButton {
                                                            variant: "outline"
                                                            text: "↓ " + root.trText("下移")
                                                            Layout.preferredWidth: Math.max(82, implicitWidth)
                                                            enabled: settingsVM.pluginModel.canMoveDown(
                                                                         detailPanel.currentRow)
                                                            onClicked: settingsVM.pluginModel.moveDown(
                                                                           detailPanel.currentRow)
                                                        }
                                                    }
                                                }
                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: UiMetrics.space1
                                                    Text { text: root.trText("宽度"); color: root.c.fg; font.pixelSize: UiMetrics.fontS; font.weight: Font.DemiBold }
                                                    Text {
                                                        text: Number(detailPanel.rowData.width) > 0
                                                              ? root.trText("固定插件占用宽度")
                                                              : root.trText("自动跟随内容宽度")
                                                        color: root.c.muted
                                                        font.pixelSize: UiMetrics.fontTiny
                                                    }
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: UiMetrics.space2
                                                        UiComboBox {
                                                            model: [root.trText("自适应"), root.trText("手动")]
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
                                                    }
                                                }
                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    visible: Object.keys(detailPanel.rowData.layoutPreference || {}).length > 0
                                                    spacing: UiMetrics.space1
                                                    Text {
                                                        text: root.trText("空间不足时")
                                                        color: root.c.fg
                                                        font.pixelSize: UiMetrics.fontS
                                                        font.weight: Font.DemiBold
                                                    }
                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: {
                                                            const layout = detailPanel.rowData.layoutPreference || {}
                                                            if (String(layout.overflow) === "menu")
                                                                return root.trText("先收缩，仍放不下时移入更多插件")
                                                            if (String(layout.overflow) === "hide")
                                                                return root.trText("先收缩，仍放不下时自动隐藏")
                                                            return root.trText("收缩到插件允许的最小宽度后保持显示")
                                                        }
                                                        color: root.c.muted
                                                        font.pixelSize: UiMetrics.fontTiny
                                                        wrapMode: Text.Wrap
                                                    }
                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: Number(detailPanel.rowData.width) > 0
                                                              ? root.trText("当前手动宽度会覆盖此响应式策略")
                                                              : root.trText("使用自适应宽度时由插件策略自动处理")
                                                        color: root.c.accent
                                                        font.pixelSize: UiMetrics.fontTiny
                                                        wrapMode: Text.Wrap
                                                    }
                                                }
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: (detailPanel.rowData.settingsQml !== undefined
                                                      && String(detailPanel.rowData.settingsQml).length > 0)
                                                     || (detailPanel.rowData.schema || []).length > 0
                                            Text {
                                                text: root.trText("插件设置")
                                                color: root.c.fg
                                                font.pixelSize: UiMetrics.fontL
                                                font.weight: Font.DemiBold
                                            }
                                            Item { Layout.fillWidth: true }
                                            Text {
                                                text: detailPanel.rowData.settingsQml !== undefined
                                                      && String(detailPanel.rowData.settingsQml).length > 0
                                                      ? root.trText("自定义设置页") : root.trText("通用设置项")
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
                                                text: "⚠ " + root.trText("设置页加载失败（详见 xuanhub.log）")
                                            }
                                        }

                                        // settings schema 生成的分组表单（无自绘页时）
                                        Repeater {
                                            model: customPage.visible ? [] : (detailPanel.rowData.schema || [])
                                            delegate: groupDelegate
                                        }
                                    }

                                    UiCard {
                                        Layout.fillWidth: true
                                        compact: true
                                        visible: detailPanel.rowData.enabled === true
                                                 && Object.keys(detailPanel.rowData.health || {}).length > 0
                                        title: root.trText("运行状态")
                                        description: root.trText("显示插件当前健康状态和最近一次运行错误")

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: UiMetrics.space2
                                            Rectangle {
                                                width: 10; height: 10; radius: 5
                                                property string state: String(
                                                    (detailPanel.rowData.health || {}).state || "discovered")
                                                color: state === "running" ? root.c.success
                                                     : (state === "failed" || state === "circuitOpen")
                                                       ? root.c.danger
                                                     : (state === "restarting" || state === "degraded")
                                                       ? root.c.warning : root.c.muted
                                            }
                                            Text {
                                                color: root.c.fg
                                                font.pixelSize: UiMetrics.fontM
                                                font.weight: Font.DemiBold
                                                text: root.healthLabel(String(
                                                    (detailPanel.rowData.health || {}).state || "discovered"))
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                color: root.c.muted
                                                font.pixelSize: UiMetrics.fontTiny
                                                text: root.trText("连续失败：%1 · 重启次数：%2 · 队列：%3")
                                                    .arg(Number((detailPanel.rowData.health || {}).consecutiveFailures || 0))
                                                    .arg(Number((detailPanel.rowData.health || {}).restartCount || 0))
                                                    .arg(Number((detailPanel.rowData.health || {}).queueDepth || 0))
                                                elide: Text.ElideRight
                                            }
                                            UiButton {
                                                visible: detailPanel.rowData.kind === "python"
                                                         && (detailPanel.rowData.dependencyPlan || {}).available !== true
                                                variant: "outline"
                                                compact: true
                                                text: root.trText("重新启动插件")
                                                Layout.preferredWidth: Math.max(112, implicitWidth)
                                                onClicked: settingsVM.restartPlugin(pluginList.currentId)
                                            }
                                            UiButton {
                                                visible: detailPanel.rowData.kind === "python"
                                                         && (detailPanel.rowData.dependencyPlan || {}).available === true
                                                enabled: (detailPanel.rowData.dependencyPlan || {}).installing !== true
                                                variant: "primary"
                                                compact: true
                                                text: (detailPanel.rowData.dependencyPlan || {}).installing === true
                                                    ? root.trText("正在安装…")
                                                    : root.trText("安装离线依赖")
                                                Layout.preferredWidth: Math.max(128, implicitWidth)
                                                onClicked: dependencyConfirm.ask(
                                                    pluginList.currentId,
                                                    detailPanel.rowData.dependencyPlan)
                                            }
                                            UiButton {
                                                variant: "outline"
                                                compact: true
                                                text: root.trText("导出诊断")
                                                onClicked: {
                                                    const path = settingsCtl.choosePluginDiagnostics(
                                                        pluginList.currentId)
                                                    if (path.length > 0)
                                                        settingsVM.exportPluginDiagnostics(
                                                            pluginList.currentId, path)
                                                }
                                            }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            color: root.c.muted
                                            font.pixelSize: UiMetrics.fontTiny
                                            wrapMode: Text.Wrap
                                            text: {
                                                const health = detailPanel.rowData.health || ({})
                                                const parts = [
                                                    root.trText("启动耗时：%1 ms").arg(
                                                        Number(health.loadDurationMs || 0)),
                                                    root.trText("最近成功：%1").arg(
                                                        root.healthTime(health.lastHealthyAt))
                                                ]
                                                if (detailPanel.rowData.kind === "python"
                                                        && Number(health.processId || 0) > 0) {
                                                    parts.push(root.trText("Python PID：%1").arg(
                                                        Number(health.processId)))
                                                }
                                                return parts.join(" · ")
                                            }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            visible: String((detailPanel.rowData.health || {}).lastError || "").length > 0
                                            spacing: UiMetrics.space1
                                            Text {
                                                text: String((detailPanel.rowData.health || {}).lastErrorCode || "").length > 0
                                                    ? root.trText("最近错误（%1）").arg(String(
                                                        (detailPanel.rowData.health || {}).lastErrorCode))
                                                    : root.trText("最近错误")
                                                color: root.c.danger
                                                font.pixelSize: UiMetrics.fontS
                                                font.weight: Font.DemiBold
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: String((detailPanel.rowData.health || {}).lastError || "")
                                                color: root.c.muted
                                                font.pixelSize: UiMetrics.fontTiny
                                                wrapMode: Text.Wrap
                                            }
                                        }
                                    }

                                    UiCard {
                                        Layout.fillWidth: true
                                        compact: true
                                        title: root.trText("权限与安全")
                                        description: root.trText("查看插件声明、实际授权和运行隔离级别")

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: UiMetrics.space2
                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: UiMetrics.space2
                                                Rectangle {
                                                    Layout.preferredWidth: trustTag.implicitWidth + 18
                                                    Layout.preferredHeight: 24
                                                    radius: 12
                                                    color: root.translucent(
                                                        detailPanel.rowData.trustLevel === "isolated"
                                                            ? root.accentColor : root.c.warning, 0.16)
                                                    Text {
                                                        id: trustTag
                                                        anchors.centerIn: parent
                                                        color: detailPanel.rowData.trustLevel === "isolated"
                                                            ? root.c.accent : root.c.warning
                                                        font.pixelSize: UiMetrics.fontTiny
                                                        font.weight: Font.DemiBold
                                                        text: detailPanel.rowData.trustLevel === "isolated"
                                                            ? root.trText("隔离插件")
                                                            : root.trText("受信任的进程内插件")
                                                    }
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    color: root.c.muted
                                                    font.pixelSize: UiMetrics.fontS
                                                    wrapMode: Text.Wrap
                                                    text: detailPanel.rowData.trustLevel === "isolated"
                                                        ? root.trText("Python 后端运行在独立进程中，HostServices 请求按声明权限强制校验。")
                                                        : root.trText("原生插件与宿主在同一进程运行，权限仅作说明，不能限制其直接访问系统资源。")
                                                }
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                color: root.c.fg
                                                font.pixelSize: UiMetrics.fontS
                                                wrapMode: Text.Wrap
                                                text: root.trText("声明权限：%1").arg(
                                                    root.permissionListText(detailPanel.rowData.declaredPermissions))
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                color: root.c.fg
                                                font.pixelSize: UiMetrics.fontS
                                                wrapMode: Text.Wrap
                                                text: root.trText("已授予权限：%1").arg(
                                                    root.permissionListText(detailPanel.rowData.grantedPermissions))
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                color: root.c.muted
                                                font.pixelSize: UiMetrics.fontTiny
                                                wrapMode: Text.Wrap
                                                text: root.trText("宿主服务：API %1 · %2").arg(
                                                    Number(detailPanel.rowData.apiVersion || 1)).arg(
                                                    root.permissionListText(detailPanel.rowData.capabilities))
                                            }
                                        }
                                    }
                                }
                            }

                            // ── 分组与字段编辑器组件（schema 表单用）──
                            Component {
                                id: groupDelegate
                                UiCard {
                                    id: groupRoot
                                    Layout.fillWidth: true
                                    property var groupData: modelData
                                    property bool collapsed: false

                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: 32
                                        radius: UiMetrics.radiusS
                                        color: groupMa.containsMouse
                                               ? root.translucent(root.fgColor, 0.06)
                                               : "transparent"
                                        Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }
                                        Text {
                                            anchors.left: parent.left
                                            anchors.leftMargin: UiMetrics.space2
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: groupRoot.groupData.group || root.trText("设置")
                                            color: root.c.fg
                                            font.pixelSize: UiMetrics.fontL
                                            font.weight: Font.DemiBold
                                        }
                                        Text {
                                            anchors.right: parent.right
                                            anchors.rightMargin: UiMetrics.space2
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: groupRoot.collapsed ? "▸" : "▾"
                                            color: root.c.muted
                                            font.pixelSize: UiMetrics.fontM
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
                                    spacing: UiMetrics.space1
                                    property var field: modelData
                                    visible: root.schemaConditionMatches(field.visibleWhen)
                                    enabled: root.schemaConditionMatches(field.enabledWhen)

                                    Text {
                                        text: field.label || field.key
                                        color: root.c.fg
                                        font.pixelSize: UiMetrics.fontS
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        visible: !!field.description
                                        text: field.description || ""
                                        color: root.c.muted
                                        font.pixelSize: UiMetrics.fontTiny
                                        Layout.fillWidth: true
                                        wrapMode: Text.Wrap
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
                                                case "object": return jsonFieldComp
                                                case "array":  return jsonFieldComp
                                                case "json":   return jsonFieldComp
                                                default:         return field.multiline === true
                                                                        ? multilineFieldComp
                                                                        : stringFieldComp
                                            }
                                        }
                                        onLoaded: item.field = field
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
                                    echoMode: field.secret === true ? TextInput.Password : TextInput.Normal
                                    Component.onDestruction: if (invalid) root.invalidFields -= 1
                                    function validText(value) {
                                        if (!field.regex) return true
                                        try { return new RegExp(String(field.regex)).test(value) }
                                        catch (e) { return false }
                                    }
                                    Component.onCompleted: init()
                                    onFieldChanged: init()
                                    function init() {
                                        if (!field.key) return
                                        _initializing = true
                                        text = String(root.draftValue(field.key,
                                            field.default !== undefined ? field.default : ""))
                                        _initializing = false
                                        root.setFieldInvalid(this, !validText(text))
                                    }
                                    onTextChanged: {
                                        if (!field.key || _initializing) return
                                        const ok = validText(text)
                                        root.setFieldInvalid(this, !ok)
                                        if (ok) settingsVM.pluginProxy.setValue(field.key, text)
                                    }
                                }
                            }

                            Component {
                                id: multilineFieldComp
                                TextArea {
                                    property var field: ({})
                                    property bool invalid: false
                                    property bool _initializing: false
                                    Component.onDestruction: if (invalid) root.invalidFields -= 1
                                    implicitHeight: Math.min(180, Math.max(88, contentHeight + 24))
                                    placeholderText: field.placeholder || ""
                                    wrapMode: TextEdit.Wrap
                                    selectByMouse: true
                                    color: root.c.fg
                                    function validText(value) {
                                        if (!field.regex) return true
                                        try { return new RegExp(String(field.regex)).test(value) }
                                        catch (e) { return false }
                                    }
                                    Component.onCompleted: init()
                                    onFieldChanged: init()
                                    function init() {
                                        if (!field.key) return
                                        _initializing = true
                                        text = String(root.draftValue(field.key,
                                            field.default !== undefined ? field.default : ""))
                                        _initializing = false
                                        root.setFieldInvalid(this, !validText(text))
                                    }
                                    onTextChanged: {
                                        if (!field.key || _initializing) return
                                        const ok = validText(text)
                                        root.setFieldInvalid(this, !ok)
                                        if (ok) settingsVM.pluginProxy.setValue(field.key, text)
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
                                        placeholderText: colorRow.field.placeholder || root.trText("留空跟随默认")
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
                                    textRole: "label"
                                    valueRole: "value"
                                    Component.onCompleted: init()
                                    onFieldChanged: init()
                                    function init() {
                                        if (!field.key) return
                                        const cur = String(root.draftValue(field.key,
                                            field.default !== undefined ? field.default : ""))
                                        const options = field.options || []
                                        let selected = -1
                                        for (let i = 0; i < options.length; ++i) {
                                            if (String(options[i].value) === cur) {
                                                selected = i
                                                break
                                            }
                                        }
                                        currentIndex = selected >= 0 ? selected : 0
                                    }
                                    onActivated: if (field.key) settingsVM.pluginProxy.setValue(
                                                     field.key, currentValue)
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
                                        if (ok && field.type === "object")
                                            ok = parsed !== null && typeof parsed === "object"
                                                 && !Array.isArray(parsed)
                                        if (ok && field.type === "array") ok = Array.isArray(parsed)
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
                            x: UiMetrics.space5
                            y: UiMetrics.space5
                            width: generalScroll.availableWidth - UiMetrics.space5 * 2
                            spacing: UiMetrics.space4

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: UiMetrics.space1
                                Text {
                                    text: root.trText("通用")
                                    color: root.c.fg
                                    font.pixelSize: UiMetrics.fontXL
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: root.trText("管理界面语言与 Windows 启动方式")
                                    color: root.c.muted
                                    font.pixelSize: UiMetrics.fontM
                                }
                            }

                            UiCard {
                                Layout.fillWidth: true
                                title: root.trText("语言")
                                description: root.trText("默认跟随 Windows，也可以固定使用中文或英文")

                                RowLayout {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: UiMetrics.space1
                                        Text {
                                            text: root.trText("界面语言")
                                            color: root.c.fg
                                            font.pixelSize: UiMetrics.fontM
                                            font.weight: Font.DemiBold
                                        }
                                        Text {
                                            text: root.trText("保存后立即更新所有已打开界面")
                                            color: root.c.muted
                                            font.pixelSize: UiMetrics.fontTiny
                                        }
                                    }
                                    UiComboBox {
                                        Layout.preferredWidth: 220
                                        model: [
                                            { value: "system", label: root.trText("跟随系统") },
                                            { value: "zh-CN", label: "简体中文" },
                                            { value: "en-US", label: "English" }
                                        ]
                                        textRole: "label"
                                        valueRole: "value"
                                        currentIndex: settingsVM.language === "zh-CN" ? 1
                                                      : settingsVM.language === "en-US" ? 2 : 0
                                        onActivated: settingsVM.language = currentValue
                                    }
                                }
                            }

                            UiCard {
                                Layout.fillWidth: true
                                title: root.trText("系统")
                                description: root.trText("登录 Windows 后自动启动玄枢状态栏")

                                RowLayout {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: UiMetrics.space1
                                        Text {
                                            text: root.trText("开机自启")
                                            color: root.c.fg
                                            font.pixelSize: UiMetrics.fontM
                                            font.weight: Font.DemiBold
                                        }
                                        Text {
                                            text: root.trText("通过当前用户的 Windows 启动项运行")
                                            color: root.c.muted
                                            font.pixelSize: UiMetrics.fontTiny
                                        }
                                    }
                                    UiSwitch {
                                        text: settingsVM.autoStart ? root.trText("已开启")
                                                                           : root.trText("已关闭")
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
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            color: root.panelColor

            Text {
                id: footerError
                anchors.left: parent.left
                anchors.leftMargin: UiMetrics.space5
                anchors.right: actionButtons.left
                anchors.rightMargin: UiMetrics.space4
                anchors.verticalCenter: parent.verticalCenter
                // 错误文本与按钮组使用锚定而非同一个 RowLayout：错误不存在时也不会
                // 占用弹性宽度，从而始终让三个操作按钮紧凑地靠右对齐。
                text: settingsVM.errorMessage.length > 0 ? settingsVM.errorMessage
                    : (root.invalidFields > 0
                       ? root.trText("存在未修正的非法输入（红框项），修正后才能保存") : "")
                color: root.c.danger
                font.pixelSize: UiMetrics.fontS
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                visible: text.length > 0
            }

            RowLayout {
                id: actionButtons
                anchors.right: parent.right
                anchors.rightMargin: UiMetrics.space5
                anchors.verticalCenter: parent.verticalCenter
                spacing: UiMetrics.space2

                UiButton {
                    variant: "ghost"
                    text: root.trText("取消")
                    Layout.preferredWidth: Math.max(72, implicitWidth)
                    onClicked: Window.window.close()              // 隐藏，不保存
                }
                UiButton {
                    variant: "outline"
                    text: root.trText("应用")
                    Layout.preferredWidth: Math.max(76, implicitWidth)
                    enabled: settingsVM.errorMessage.length === 0 && root.invalidFields === 0
                    onClicked: settingsVM.apply()                  // 保存但不关闭
                }
                UiButton {
                    variant: "primary"
                    text: root.trText("保存并关闭")
                    Layout.preferredWidth: Math.max(128, implicitWidth)
                    enabled: root.invalidFields === 0
                    onClicked: if (settingsVM.apply()) Window.window.close()
                }
            }
        }
    }
}
