// SysInfoSettings.qml — sysinfo 插件自绘设置页（M10 settingsQml）
// 契约：根 Item 声明 property var plugin（设置窗口注入 SettingsPluginProxy）；
// plugin.value("columns"/"items") 读草稿、plugin.setValue(...) 整体写回。
// 语义铁律：只读写草稿——应用/保存由设置窗口统一落盘，取消丢弃，热重载后生效。
// 控件配色经 palette 继承自设置窗口（Basic 样式），无需自取 themeManager。
//
// 布局：列数 SpinBox + 指标项列表（每行：metric 下拉 + 前缀 + 颜色 + ↑↓✕）+ 添加按钮。
// items 工作副本范式照搬 LauncherSettings：文本编辑原地改（不重建保焦点），
// 增删移才整体替换数组触发 Repeater 重建。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var plugin

    implicitHeight: col.implicitHeight

    // 可选指标（metric id → 显示名）；"none"=该项临时不显示（config 行保留，改回即恢复）
    readonly property var metricOptions: ["none", "cpu", "mem", "gpu", "netDown", "netUp"]
    readonly property var metricLabels: ["不显示", "CPU", "内存", "GPU", "下行", "上行"]

    // items 工作副本：[{metric, prefix, color}]
    property var items: []
    property int itemsPerColumn: 5

    Component.onCompleted: {
        const c = plugin ? plugin.value("itemsPerColumn") : undefined
        itemsPerColumn = (c !== undefined && c !== null) ? c : 5
        const v = plugin ? plugin.value("items") : undefined
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
        spacing: 8

        // ── 每列显示项数 ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "每列显示项数"
                color: palette.windowText
                font.pixelSize: 12
            }
            SpinBox {
                from: 1
                to: 10
                value: root.itemsPerColumn
                onValueModified: { root.itemsPerColumn = value; root.commit() }
            }
            Label {
                text: "（控制状态栏里每列排几个指标，超出向右另起一列）"
                color: palette.placeholderText
                font.pixelSize: 11
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }

        // ── 指标项列表标题 ──
        Label {
            text: "显示项（顺序即状态栏排列顺序）"
            color: palette.windowText
            font.pixelSize: 12
            font.bold: true
        }

        Repeater {
            model: root.items

            delegate: RowLayout {
                Layout.fillWidth: true
                spacing: 6

                // metric 下拉
                // value 一次性设（Component.onCompleted），不绑 modelData：
                // Repeater 的 modelData 是瞬态引用，原地改 items[i] 后不会重发，
                // 绑定会把显示值"拉回"旧值——故沿用 schema 表单的 init 范式
                ComboBox {
                    Layout.preferredWidth: 100
                    model: root.metricLabels
                    Component.onCompleted: {
                        const mi = root.metricOptions.indexOf(modelData.metric)
                        currentIndex = mi >= 0 ? mi : 0
                    }
                    onActivated: root.updateRow(index, "metric", root.metricOptions[currentIndex])
                }
                // 前缀
                TextField {
                    Layout.preferredWidth: 80
                    text: modelData.prefix
                    placeholderText: "前缀"
                    selectByMouse: true
                    onTextChanged: root.updateRow(index, "prefix", text)
                }
                // 颜色（留空跟随主题）
                TextField {
                    Layout.preferredWidth: 80
                    text: modelData.color
                    placeholderText: "颜色（空=主题）"
                    selectByMouse: true
                    onTextChanged: root.updateRow(index, "color", text)
                }
                // 颜色预览色块
                Rectangle {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    radius: 3
                    border.width: 1
                    border.color: palette.mid
                    color: /^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/
                           .test(modelData.color) ? modelData.color : "transparent"
                }
                // 上移
                Button {
                    text: "↑"; flat: true; enabled: index > 0
                    onClicked: root.moveRow(index, -1)
                }
                // 下移
                Button {
                    text: "↓"; flat: true; enabled: index < root.items.length - 1
                    onClicked: root.moveRow(index, 1)
                }
                // 删除
                Button {
                    text: "✕"; flat: true
                    onClicked: root.removeRow(index)
                }
            }
        }

        Button {
            text: "＋ 添加指标项"
            onClicked: root.addRow()
        }

        Label {
            visible: root.items.length === 0
            text: "至少添加一个指标项，否则状态栏不显示任何内容"
            color: palette.placeholderText
            font.pixelSize: 11
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
