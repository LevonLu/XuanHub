// SysInfoSettings.qml — sysinfo 插件自绘设置页（M10 settingsQml）
// 契约：根 Item 声明 property var plugin（设置窗口注入 SettingsPluginProxy）；
// plugin.value("itemsPerColumn"/"items") 读草稿、plugin.setValue(...) 整体写回。
// 语义铁律：只读写草稿——应用/保存由设置窗口统一落盘，取消丢弃，热重载后生效。
// 控件配色经 palette 继承自设置窗口（FluentWinUI3 样式），无需自取 themeManager。
//
// 布局：列数 SpinBox + 指标项列表（每行：metric 下拉 + 前缀 + 颜色 + ↑↓✕）+ 添加按钮。
// items 工作副本范式：文本编辑原地改（不重建保焦点），增删移才整体替换数组触发 Repeater 重建。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub   // UiComboBox：裸 ComboBox 的 popup 是独立顶层窗口，palette 继承断裂

Item {
    id: root
    property var plugin
    function trText(source) { return qsTranslate("plugin.sysinfo.settingsPage", source) }

    implicitHeight: col.implicitHeight

    // 可选指标（metric id → 显示名）；"none"=该项临时不显示（config 行保留，改回即恢复）
    readonly property var metricOptions: ["none", "cpu", "mem", "gpu", "netDown", "netUp"]
    readonly property var metricLabels: [
        trText("不显示"), "CPU", trText("内存"), "GPU", trText("下行"), trText("上行")
    ]

    // items 工作副本：[{metric, prefix, color}]
    property var items: []
    property int itemsPerColumn: 5
    // 初始化必须在 onPluginChanged（onCompleted 时 plugin 必为 undefined，
    // 会拿默认值覆盖用户已存配置）；_loaded 防重复注入重置副本
    property bool _loaded: false

    onPluginChanged: {
        if (!plugin || _loaded) return
        _loaded = true
        const c = plugin.value("itemsPerColumn")
        itemsPerColumn = (c !== undefined && c !== null) ? c : 1
        const v = plugin.value("items")
        items = (v !== undefined && v !== null) ? v : defaultItems()
    }

    // 默认布局（config 无 items 时）
    function defaultItems() {
        return [
            { metric: "cpu",     prefix: "CPU ", color: "" },
            { metric: "mem",     prefix: "RAM ", color: "" },
            { metric: "gpu",     prefix: "GPU ", color: "" },
            { metric: "netDown", prefix: "↓ ",   color: "" },
            { metric: "netUp",   prefix: "↑ ",   color: "" }
        ]
    }

    // 整体写回草稿（items 用 slice(0) 浅拷贝，确保 proxy 拿到新 QVariantList）
    function commit() {
        if (!plugin) return
        plugin.setValue("itemsPerColumn", itemsPerColumn)
        plugin.setValue("items", items.slice(0))
    }

    function metricLabel(id) {
        const i = metricOptions.indexOf(id)
        return i >= 0 ? metricLabels[i] : id
    }

    // 原地改某行字段（文本编辑不重建列表，保住输入焦点）
    function updateRow(i, key, val) { items[i][key] = val; commit() }
    function addRow() {
        items = items.concat([{ metric: "cpu", prefix: "CPU ", color: "" }])
        commit()
    }
    function removeRow(i) { const a = items.slice(0); a.splice(i, 1); items = a; commit() }
    function moveRow(i, d) {
        const j = i + d
        if (j < 0 || j >= items.length) return
        const a = items.slice(0)
        const t = a[i]; a[i] = a[j]; a[j] = t
        items = a
        commit()
    }

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: UiMetrics.space4

        UiCard {
            Layout.fillWidth: true
            title: root.trText("布局")
            description: root.trText("控制指标在状态栏中如何分列排列")

            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: UiMetrics.space1
                    Text {
                        text: root.trText("每列显示项数")
                        color: palette.windowText
                        font.pixelSize: UiMetrics.fontS
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: root.trText("超出后会从右侧开始新的一列")
                        color: palette.placeholderText
                        font.pixelSize: UiMetrics.fontTiny
                    }
                }
                UiSpinBox {
                    Layout.preferredWidth: 132
                    from: 1
                    to: 10
                    value: root.itemsPerColumn
                    onValueModified: { root.itemsPerColumn = value; root.commit() }
                }
            }
        }

        UiCard {
            Layout.fillWidth: true
            title: root.trText("显示项")
            description: root.trText("用上下按钮调整顺序；颜色留空时跟随主题。")

            Repeater {
                model: root.items

                delegate: Rectangle {
                    id: metricRow
                    objectName: "sysinfoMetricRow_" + index
                    readonly property int rowIndex: index
                    readonly property bool inlineActions: width >= 440
                    Layout.fillWidth: true
                    implicitHeight: rowGrid.implicitHeight + UiMetrics.space4
                    radius: UiMetrics.radiusS
                    color: Qt.rgba(palette.base.r, palette.base.g, palette.base.b, 0.72)
                    border.width: 1
                    border.color: Qt.rgba(palette.mid.r, palette.mid.g, palette.mid.b, 0.36)

                    // 操作区保留固定预算，字段消化剩余宽度；窄行把操作放到第二行。
                    // 不靠裁剪掩盖越界，删除按钮始终留在卡片内部并可点击。
                    GridLayout {
                        id: rowGrid
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.topMargin: UiMetrics.space2
                        anchors.leftMargin: UiMetrics.space2
                        anchors.rightMargin: UiMetrics.space2
                        columns: metricRow.inlineActions ? 2 : 1
                        columnSpacing: UiMetrics.space2
                        rowSpacing: UiMetrics.space1

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: UiMetrics.space2
                            // value 一次性设：文本编辑期间不重建列表，保留输入焦点。
                            UiComboBox {
                                objectName: "sysinfoMetric_" + metricRow.rowIndex
                                Layout.fillWidth: true
                                Layout.minimumWidth: 96
                                Layout.preferredWidth: 108
                                model: root.metricLabels
                                Component.onCompleted: {
                                    const mi = root.metricOptions.indexOf(modelData.metric)
                                    currentIndex = mi >= 0 ? mi : 0
                                }
                                onActivated: activatedIndex => root.updateRow(metricRow.rowIndex, "metric",
                                                                              root.metricOptions[activatedIndex])
                            }
                            UiTextField {
                                objectName: "sysinfoPrefix_" + metricRow.rowIndex
                                Layout.fillWidth: true
                                Layout.minimumWidth: 56
                                Layout.preferredWidth: 78
                                text: modelData.prefix
                                placeholderText: root.trText("前缀")
                                onTextChanged: root.updateRow(metricRow.rowIndex, "prefix", text)
                            }
                            UiTextField {
                                objectName: "sysinfoColor_" + metricRow.rowIndex
                                Layout.fillWidth: true
                                Layout.minimumWidth: 80
                                Layout.preferredWidth: 112
                                text: modelData.color
                                placeholderText: root.trText("颜色")
                                onTextChanged: root.updateRow(metricRow.rowIndex, "color", text)
                            }
                            Rectangle {
                                objectName: "sysinfoSwatch_" + metricRow.rowIndex
                                Layout.minimumWidth: 22
                                Layout.maximumWidth: 22
                                Layout.preferredHeight: 22
                                radius: 5
                                border.width: 1
                                border.color: palette.mid
                                color: /^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/
                                       .test(modelData.color) ? modelData.color : "transparent"
                            }
                        }
                        RowLayout {
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            Layout.minimumWidth: 104
                            Layout.maximumWidth: 104
                            spacing: UiMetrics.space1
                            UiButton {
                                objectName: "sysinfoUp_" + metricRow.rowIndex
                                variant: "ghost"
                                text: "↑"
                                Accessible.name: root.trText("上移")
                                ToolTip.visible: hovered
                                ToolTip.text: root.trText("上移")
                                enabled: metricRow.rowIndex > 0
                                Layout.minimumWidth: 32
                                Layout.maximumWidth: 32
                                leftPadding: 0; rightPadding: 0
                                onClicked: root.moveRow(metricRow.rowIndex, -1)
                            }
                            UiButton {
                                objectName: "sysinfoDown_" + metricRow.rowIndex
                                variant: "ghost"
                                text: "↓"
                                Accessible.name: root.trText("下移")
                                ToolTip.visible: hovered
                                ToolTip.text: root.trText("下移")
                                enabled: metricRow.rowIndex < root.items.length - 1
                                Layout.minimumWidth: 32
                                Layout.maximumWidth: 32
                                leftPadding: 0; rightPadding: 0
                                onClicked: root.moveRow(metricRow.rowIndex, 1)
                            }
                            UiButton {
                                objectName: "sysinfoDelete_" + metricRow.rowIndex
                                variant: "ghost"
                                text: "×"
                                Accessible.name: root.trText("删除")
                                ToolTip.visible: hovered
                                ToolTip.text: root.trText("删除")
                                Layout.minimumWidth: 32
                                Layout.maximumWidth: 32
                                leftPadding: 0; rightPadding: 0
                                onClicked: root.removeRow(metricRow.rowIndex)
                            }
                        }
                    }
                }
            }

            UiButton {
                variant: "outline"
                text: "＋ " + root.trText("添加显示项")
                onClicked: root.addRow()
            }

            Text {
                visible: root.items.length === 0
                text: root.trText("至少添加一个显示项，否则状态栏不显示任何内容")
                color: palette.placeholderText
                font.pixelSize: UiMetrics.fontTiny
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }
}
