// UsageSettings.qml — py_usage_hub 自绘设置页（M10 settingsQml）
// 两行布局：上行选厂商/模式/启用/操作，下行填 API Key（独占整行，不会被挤压）
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
          "keyHint": "Auth Token" }
    ]

    function findTemplate(vid) {
        for (var i = 0; i < supportedVendors.length; i++)
            if (supportedVendors[i].id === vid) return supportedVendors[i]
        return null
    }

    // ── 工作副本 ─────────────────────────────────────────────
    property var vendors: []

    Component.onCompleted: loadTimer.start()

    Timer {
        id: loadTimer
        interval: 200; repeat: false
        onTriggered: {
            if (!plugin) return
            var v = plugin.value("vendors")
            if (v !== undefined && v !== null) {
                // JSON 深拷贝：将 QVariantList 转成纯 JS 数组，后续修改才能正确序列化回 C++
                vendors = JSON.parse(JSON.stringify(v))
            }
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
            if (t) { vendors[i].name = t.name; vendors[i].mode = t.modes[0]; vendors[i].apiUrl = t.apiUrl }
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
            "intervalSec": 300, "startHour": 8, "startMin": 30 }])
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
                implicitHeight: vendorCol.implicitHeight + 16
                radius: 6; color: palette.base
                border.width: 1; border.color: palette.mid

                ColumnLayout {
                    id: vendorCol
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.margins: 10; anchors.top: parent.top
                    spacing: 6

                    // ── 第一行：厂商 + 模式 + 启用 + 操作 ──────
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        ComboBox {
                            Layout.preferredWidth: 130
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

                        ComboBox {
                            Layout.preferredWidth: 60
                            model: { var t = root.findTemplate(modelData.id); return t ? t.modeLabels : [modelData.mode] }
                            currentIndex: 0; enabled: false
                        }

                        CheckBox {
                            text: "启用"
                            checked: modelData.enabled !== false
                            onCheckedChanged: root.updateField(index, "enabled", checked)
                        }

                        Item { Layout.fillWidth: true }

                        Button { text: "↑"; flat: true; enabled: index > 0; onClicked: root.moveVendor(index, -1) }
                        Button { text: "↓"; flat: true; enabled: index < root.vendors.length - 1; onClicked: root.moveVendor(index, 1) }
                        Button { text: "✕"; flat: true; onClicked: root.removeVendor(index) }
                    }

                    // ── 第二行：显示名称 + API Key ────────────
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        TextField {
                            Layout.preferredWidth: 100
                            text: modelData.name || ""
                            placeholderText: "显示名称"
                            selectByMouse: true
                            onTextChanged: root.updateField(index, "name", text)
                        }

                        TextField {
                            Layout.fillWidth: true
                            text: modelData.apiKey || ""
                            placeholderText: { var t = root.findTemplate(modelData.id); return t ? t.keyHint : "API Key" }
                            selectByMouse: true
                            echoMode: TextInput.Password
                            onTextChanged: root.updateField(index, "apiKey", text)
                        }
                    }

                    // ── 第三行：查询间隔 ────────────────────
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            text: "查询间隔:"
                            color: palette.windowText
                            font.pixelSize: 11
                        }

                        SpinBox {
                            Layout.preferredWidth: 80
                            from: 30; to: 86400; stepSize: 30
                            value: modelData.intervalSec || 300
                            onValueChanged: root.updateField(index, "intervalSec", value)
                        }

                        Label {
                            text: "秒"
                            color: palette.windowText
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }

        Button { text: "＋ 添加厂商"; onClicked: root.addVendor() }
    }
}
