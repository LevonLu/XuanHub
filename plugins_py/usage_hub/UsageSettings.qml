// UsageSettings.qml — py_usage_hub 自绘设置页（M10 settingsQml）
// 每厂商一张卡片三行：厂商+模式徽章+启用+排序/删除 ／ 显示名称+API Key ／ 查询间隔（秒）
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub   // UiComboBox：裸 ComboBox 的 popup 是独立顶层窗口，palette 继承断裂

Item {
    id: root
    property var plugin
    implicitHeight: col.implicitHeight

    // ── 厂商模板 ──────────────────────────────────────────────
    property var supportedVendors: [
        { "id": "deepseek", "name": "DeepSeek",
          "modes": ["balance"], "modeLabels": ["余额"],
          "apiUrl": "https://api.deepseek.com/user/balance",
          "keyHint": "API Key (sk-...)" },
        { "id": "glm", "name": "GLM (智谱)",
          "modes": ["plan"], "modeLabels": ["Plan"],
          "apiUrl": "https://open.bigmodel.cn",
          "keyHint": "Auth Token" },
        { "id": "opencode_go", "name": "OpenCode Go",
          "modes": ["go"], "modeLabels": ["Go"],
          "apiUrl": "https://opencode.ai",
          "keyHint": "Cookie",
          "hasWorkspace": true }
    ]

    function findTemplate(vid) {
        for (var i = 0; i < supportedVendors.length; i++)
            if (supportedVendors[i].id === vid) return supportedVendors[i]
        return null
    }

    // ── 工作副本 ─────────────────────────────────────────────
    property var vendors: []

    // 初始化必须在 onPluginChanged（onCompleted 时 plugin 必为 undefined）；
    // 守卫 pluginId——注入晚到时 proxy 可能已改指别的插件，不校验会把别的
    // 插件的 vendors 读进来、后续 commit 也会写进别人草稿（契约见 plugins_py/README）
    onPluginChanged: {
        if (!plugin || plugin.pluginId !== "py_usage_hub") return
        var v = plugin.value("vendors")
        if (v !== undefined && v !== null) {
            // JSON 深拷贝：将 QVariantList 转成纯 JS 数组，后续修改才能正确序列化回 C++
            vendors = JSON.parse(JSON.stringify(v))
        }
    }

    function commit() {
        // JSON 深拷贝写出：确保 JS 对象被序列化为干净的结构传给 C++
        if (plugin) plugin.setValue("vendors", JSON.parse(JSON.stringify(vendors)))
    }

    function updateField(i, key, val) {
        vendors[i][key] = val
        if (key === "id") {
            var t = findTemplate(val)
            if (t) { vendors[i].name = t.name; vendors[i].mode = t.modes[0]; vendors[i].apiUrl = t.apiUrl; vendors[i].workspace = "" }
            // 切换厂商是结构性变化，需要重建 Repeater
            vendors = vendors.slice()
        }
        // 普通字段修改只做原地写入 + 持久化，不重建 Repeater（避免失焦）
        commit()
    }

    function addVendor() {
        var f = supportedVendors[0]
        vendors = vendors.concat([{ "id": f.id, "name": f.name, "mode": f.modes[0],
            "enabled": true, "apiKey": "", "apiUrl": f.apiUrl,
            "intervalSec": 300, "startHour": 8, "startMin": 30, "workspace": "" }])
        commit()
    }

    function removeVendor(i) { var a = vendors.slice(0); a.splice(i, 1); vendors = a; commit() }

    function moveVendor(i, d) {
        var j = i + d; if (j < 0 || j >= vendors.length) return
        var a = vendors.slice(0); var t = a[i]; a[i] = a[j]; a[j] = t; vendors = a; commit()
    }

    // ── UI ──────────────────────────────────────────────────
    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 8

        Label {
            visible: root.vendors.length === 0
            text: "暂无厂商，点击下方按钮添加"
            color: palette.placeholderText; font.pixelSize: 12
        }

        Repeater {
            model: root.vendors

            delegate: Rectangle {
                Layout.fillWidth: true
                implicitHeight: vendorCol.implicitHeight + UiMetrics.space4 * 2
                radius: UiMetrics.radiusM
                color: Qt.rgba(palette.base.r, palette.base.g, palette.base.b, 0.72)
                border.width: 1
                border.color: Qt.rgba(palette.mid.r, palette.mid.g, palette.mid.b, 0.36)

                ColumnLayout {
                    id: vendorCol
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.margins: UiMetrics.space4; anchors.top: parent.top
                    spacing: UiMetrics.space2

                    // ── 第一行：厂商 + 模式徽章 …… 启用 + 排序/删除 ──
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: UiMetrics.space2

                        UiComboBox {
                            Layout.preferredWidth: 150
                            textRole: "name"; valueRole: "id"
                            model: root.supportedVendors
                            Component.onCompleted: {
                                for (var k = 0; k < root.supportedVendors.length; k++)
                                    if (root.supportedVendors[k].id === modelData.id) { currentIndex = k; break }
                            }
                            onCurrentIndexChanged: {
                                if (currentIndex >= 0) root.updateField(index, "id", model[currentIndex].id)
                            }
                        }

                        // 模式由厂商模板决定、不可改：用 muted 文字徽章展示
                        //（原为 60px 禁用下拉框，"余额" 被裁成 "余"）
                        Rectangle {
                            radius: 10
                            color: Qt.rgba(palette.highlight.r, palette.highlight.g,
                                           palette.highlight.b, 0.14)
                            implicitWidth: modeBadge.implicitWidth + 12
                            implicitHeight: 22
                            Text {
                                id: modeBadge
                                anchors.centerIn: parent
                                text: { var t = root.findTemplate(modelData.id); return t ? t.modeLabels[0] : modelData.mode }
                                color: palette.highlight
                                font.pixelSize: UiMetrics.fontTiny
                                font.weight: Font.DemiBold
                            }
                        }

                        Item { Layout.fillWidth: true }

                        UiSwitch {
                            text: checked ? "已启用" : "已停用"
                            checked: modelData.enabled !== false
                            onToggled: root.updateField(index, "enabled", checked)
                        }

                        // 紧凑操作钮（默认 Basic 按钮太宽，三个会撑出大片空当）
                        RowLayout {
                            spacing: UiMetrics.space1
                            UiButton { variant: "ghost"; text: "↑"; Layout.preferredWidth: 36; leftPadding: 0; rightPadding: 0; enabled: index > 0; onClicked: root.moveVendor(index, -1) }
                            UiButton { variant: "ghost"; text: "↓"; Layout.preferredWidth: 36; leftPadding: 0; rightPadding: 0; enabled: index < root.vendors.length - 1; onClicked: root.moveVendor(index, 1) }
                            UiButton { variant: "ghost"; text: "×"; Layout.preferredWidth: 36; leftPadding: 0; rightPadding: 0; onClicked: root.removeVendor(index) }
                        }
                    }

                    // ── 第二行：显示名称 + API Key ────────────
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: UiMetrics.space2

                        UiTextField {
                            Layout.fillWidth: false
                            Layout.preferredWidth: 110
                            text: modelData.name || ""
                            placeholderText: "显示名称"
                            selectByMouse: true
                            onTextChanged: root.updateField(index, "name", text)
                        }

                        UiTextField {
                            Layout.fillWidth: true
                            text: modelData.apiKey || ""
                            placeholderText: { var t = root.findTemplate(modelData.id); return t ? t.keyHint : "API Key" }
                            selectByMouse: true
                            echoMode: TextInput.Password
                            onTextChanged: root.updateField(index, "apiKey", text)
                        }
                    }

                    // ── 第三行：查询间隔（秒，单位内嵌输入框）─────
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: UiMetrics.space2

                        Label {
                            text: "查询间隔"
                            color: palette.windowText
                            font.pixelSize: 11
                        }

                        UiSpinBox {
                            Layout.fillWidth: false
                            Layout.preferredWidth: 110
                            from: 30; to: 86400; stepSize: 30
                            value: modelData.intervalSec || 300
                            textFromValue: function(v) { return v + " 秒" }
                            valueFromText: function(t) { return parseInt(t) || 300 }
                            onValueChanged: root.updateField(index, "intervalSec", value)
                        }
                    }

                    // ── 第四行：Workspace（仅 hasWorkspace 厂商需要，如 OpenCode Go）──
                    RowLayout {
                        Layout.fillWidth: true
                        visible: { var t = root.findTemplate(modelData.id); return t ? !!t.hasWorkspace : false }
                        spacing: UiMetrics.space2

                        Label {
                            text: "Workspace"
                            color: palette.windowText
                            font.pixelSize: 11
                        }

                        UiTextField {
                            Layout.fillWidth: true
                            text: modelData.workspace || ""
                            placeholderText: "留空使用默认"
                            selectByMouse: true
                            onTextChanged: root.updateField(index, "workspace", text)
                        }
                    }
                }
            }
        }

        UiButton { variant: "outline"; text: "＋ 添加厂商"; onClicked: root.addVendor() }
    }
}
