// LauncherSettings.qml — launcher 插件的自绘设置页（M10 settingsQml）
// 契约：根 Item 声明 property var plugin（设置窗口注入 SettingsPluginProxy）；
// plugin.value("items") 读草稿、plugin.setValue("items", arr) 整体写回。
// 语义铁律：只读写草稿——应用/保存由设置窗口统一落盘，取消丢弃，热重载后生效。
// 控件配色经 palette 继承自设置窗口（Basic 样式），无需自取 themeManager。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var plugin

    implicitHeight: col.implicitHeight

    // items 工作副本：[{name, program}]。文本编辑原地修改（列表不重建、保住输入焦点）；
    // 增/删/上下移才整体替换数组触发 Repeater 重建。任何变更都整体写回草稿。
    property var items: []

    Component.onCompleted: {
        const v = plugin ? plugin.value("items") : undefined
        items = (v !== undefined && v !== null) ? v : []
    }

    function commit() { if (plugin) plugin.setValue("items", items.slice(0)) }

    function updateRow(i, key, val) { items[i][key] = val; commit() }
    function addRow() { items = items.concat([{ "name": "新项", "program": "" }]); commit() }
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
        spacing: 6

        Label {
            visible: root.items.length === 0
            text: "暂无启动项，点击下方按钮添加"
            color: palette.placeholderText
            font.pixelSize: 12
        }

        Repeater {
            model: root.items

            delegate: Item {
                Layout.fillWidth: true
                implicitHeight: 36

                RowLayout {
                    anchors.fill: parent
                    spacing: 6

                    TextField {
                        Layout.preferredWidth: 110
                        text: modelData.name
                        placeholderText: "名称"
                        selectByMouse: true
                        onTextChanged: root.updateRow(index, "name", text)
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: modelData.program
                        placeholderText: "程序 / 命令（如 wt.exe）"
                        selectByMouse: true
                        onTextChanged: root.updateRow(index, "program", text)
                    }
                    Button {
                        text: "↑"
                        flat: true
                        enabled: index > 0
                        onClicked: root.moveRow(index, -1)
                    }
                    Button {
                        text: "↓"
                        flat: true
                        enabled: index < root.items.length - 1
                        onClicked: root.moveRow(index, 1)
                    }
                    Button {
                        text: "✕"
                        flat: true
                        onClicked: root.removeRow(index)
                    }
                }
            }
        }

        Button {
            text: "＋ 添加启动项"
            onClicked: root.addRow()
        }
    }
}
