// CountdownSettings.qml — PyCountdown 插件自绘设置页
// 两个数字输入框（时、分）+ 日期输入 + 模式切换 + 实时预览。
// 契约同 InTimeSettings.qml：根 Item 声明 property var plugin，框架注入 SettingsPluginProxy。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub   // UiComboBox：裸 ComboBox 的 popup 是独立顶层窗口，palette 继承断裂

Item {
    id: root
    property var plugin
    implicitHeight: col.implicitHeight + 16
    implicitWidth: col.implicitWidth

    // 初始化守卫：防 TextField 初始化时触发 commit 回写循环
    property bool _loading: false

    // 读 plugin.revision 建立绑定依赖：value() 是函数调用、自身无通知，
    // 引用 revision 后 setValue 触发的 valuesChanged 会让调用 v() 的绑定重算（预览实时刷新）
    function v(key) { if (!plugin) return undefined; void plugin.revision; return plugin.value(key) }
    function sv(key, val) { if (plugin) plugin.setValue(key, val) }

    // 解析 "HH:MM" → {h, m}
    function _parseTime(str) {
        if (!str) return {h: 18, m: 0}
        var parts = String(str).split(":")
        var h = parseInt(parts[0]), m = parseInt(parts[1])
        return {
            h: isNaN(h) ? 18 : Math.min(23, Math.max(0, h)),
            m: isNaN(m) ? 0  : Math.min(59, Math.max(0, m))
        }
    }

    property string currentMode: "每日重复"
    property bool isDaily: currentMode === "每日重复"

    // ── plugin 注入后初始化默认值 ──
    // 守卫：proxy 必须已指向本插件（防框架切换时序/异步路径把默认值写进别的插件）
    onPluginChanged: {
        if (!plugin || plugin.pluginId !== "py_countdown") return
        _loading = true

        if (v("mode") === undefined) sv("mode", "每日重复")
        root.currentMode = v("mode") || "每日重复"

        if (v("targetTime") === undefined) sv("targetTime", "18:00")
        if (v("targetDateTime") === undefined) sv("targetDateTime", "")
        if (v("prefix") === undefined) sv("prefix", "下班")
        if (v("suffix") === undefined) sv("suffix", "剩余")
        if (v("reachedText") === undefined) sv("reachedText", "下班啦🎉")
        if (v("reachedColor") === undefined) sv("reachedColor", "")
        if (v("intervalMs") === undefined) sv("intervalMs", 1000)

        reloadTimeFields()
        _loading = false
    }

    function reloadTimeFields() {
        var t = _parseTime(v("targetTime"))
        hoursField.text = String(t.h)
        minutesField.text = String(t.m)

        var dtStr = v("targetDateTime") || ""
        var s = String(dtStr).trim()
        var spaceIdx = s.indexOf(" ")
        if (spaceIdx > 0) {
            dateField.text = s.substring(0, spaceIdx)
            var tp = _parseTime(s.substring(spaceIdx + 1))
            dtHoursField.text = String(tp.h)
            dtMinutesField.text = String(tp.m)
        } else {
            dateField.text = s
        }
    }

    function commitTargetTime() {
        if (_loading) return
        var h = parseInt(hoursField.text) || 0
        var m = parseInt(minutesField.text) || 0
        h = Math.min(23, Math.max(0, h))
        m = Math.min(59, Math.max(0, m))
        sv("targetTime", String(h) + ":" + (m < 10 ? "0" : "") + String(m))
    }

    function commitTargetDateTime() {
        if (_loading) return
        var d = dateField.text.trim()
        if (d.length === 0) return
        var h = parseInt(dtHoursField.text) || 0
        var m = parseInt(dtMinutesField.text) || 0
        h = Math.min(23, Math.max(0, h))
        m = Math.min(59, Math.max(0, m))
        sv("targetDateTime", d + " " + String(h) + ":" + (m < 10 ? "0" : "") + String(m))
    }

    // 预览用的时间字符串
    function previewTime() {
        var h = parseInt(hoursField.text) || 0
        var m = parseInt(minutesField.text) || 0
        return String(h) + ":" + (m < 10 ? "0" : "") + String(m) + ":00"
    }

    // ═══════════════════════════════════════════
    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 12

        // ── 目标模式 ──
        GroupBox {
            title: "目标模式"
            Layout.fillWidth: true
            RowLayout {
                spacing: 8
                Label { text: "模式:"; Layout.preferredWidth: 70 }
                UiComboBox {
                    id: modeCombo
                    model: ["每日重复", "指定日期"]
                    currentIndex: root.isDaily ? 0 : 1
                    onCurrentIndexChanged: {
                        sv("mode", currentText)
                        root.currentMode = currentText
                        root.reloadTimeFields()
                    }
                }
            }
        }

        // ── 目标时间 ──
        GroupBox {
            title: "目标时间"
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 8

                // 每日模式：时 + 分 两个输入框
                RowLayout {
                    visible: root.isDaily
                    spacing: 6
                    Label { text: "每日时刻:"; Layout.preferredWidth: 70 }
                    TextField {
                        id: hoursField
                        Layout.preferredWidth: 50
                        font.pixelSize: 13
                        horizontalAlignment: TextInput.AlignHCenter
                        selectByMouse: true
                        validator: IntValidator { bottom: 0; top: 23 }
                        onTextChanged: root.commitTargetTime()
                    }
                    Label { text: "时" }
                    TextField {
                        id: minutesField
                        Layout.preferredWidth: 50
                        font.pixelSize: 13
                        horizontalAlignment: TextInput.AlignHCenter
                        selectByMouse: true
                        validator: IntValidator { bottom: 0; top: 59 }
                        onTextChanged: root.commitTargetTime()
                    }
                    Label { text: "分" }
                }

                // 指定日期模式：日期 + 时:分
                ColumnLayout {
                    visible: !root.isDaily
                    spacing: 6
                    RowLayout {
                        spacing: 6
                        Label { text: "目标日期:"; Layout.preferredWidth: 70 }
                        TextField {
                            id: dateField
                            Layout.preferredWidth: 120
                            font.pixelSize: 13
                            placeholderText: "YYYY-MM-DD"
                            selectByMouse: true
                            onTextChanged: root.commitTargetDateTime()
                        }
                    }
                    RowLayout {
                        spacing: 6
                        Label { text: "目标时刻:"; Layout.preferredWidth: 70 }
                        TextField {
                            id: dtHoursField
                            Layout.preferredWidth: 50
                            font.pixelSize: 13
                            horizontalAlignment: TextInput.AlignHCenter
                            selectByMouse: true
                            validator: IntValidator { bottom: 0; top: 23 }
                            onTextChanged: root.commitTargetDateTime()
                        }
                        Label { text: "时" }
                        TextField {
                            id: dtMinutesField
                            Layout.preferredWidth: 50
                            font.pixelSize: 13
                            horizontalAlignment: TextInput.AlignHCenter
                            selectByMouse: true
                            validator: IntValidator { bottom: 0; top: 59 }
                            onTextChanged: root.commitTargetDateTime()
                        }
                        Label { text: "分" }
                    }
                }
            }
        }

        // ── 显示 ──
        GroupBox {
            title: "显示"
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 8

                RowLayout {
                    spacing: 8
                    Label { text: "预览:"; Layout.preferredWidth: 70 }
                    Label {
                        text: {
                            var p = v("prefix") || ""
                            var s = v("suffix") || ""
                            var t = root.isDaily ? root.previewTime() : "02:30:15"
                            var parts = [p, t, s].filter(function(x) { return x.length > 0 })
                            return parts.join(" ") || "（空）"
                        }
                        color: palette.mid
                        font.italic: true
                    }
                }

                RowLayout {
                    spacing: 8
                    Label { text: "前缀:"; Layout.preferredWidth: 70 }
                    TextField {
                        Layout.fillWidth: true
                        font.pixelSize: 13
                        text: v("prefix") || ""
                        placeholderText: "倒计时前的文字，留空则不显示前缀"
                        selectByMouse: true
                        onTextChanged: sv("prefix", text)
                    }
                }
                RowLayout {
                    spacing: 8
                    Label { text: "后缀:"; Layout.preferredWidth: 70 }
                    TextField {
                        Layout.fillWidth: true
                        font.pixelSize: 13
                        text: v("suffix") || ""
                        placeholderText: "倒计时后的文字，留空则不显示后缀"
                        selectByMouse: true
                        onTextChanged: sv("suffix", text)
                    }
                }
                RowLayout {
                    spacing: 8
                    Label { text: "到点文字:"; Layout.preferredWidth: 70 }
                    TextField {
                        Layout.fillWidth: true
                        font.pixelSize: 13
                        text: v("reachedText") || "下班啦🎉"
                        placeholderText: "倒计时归零后显示"
                        selectByMouse: true
                        onTextChanged: sv("reachedText", text)
                    }
                }
                RowLayout {
                    spacing: 8
                    Label { text: "到点颜色:"; Layout.preferredWidth: 70 }
                    TextField {
                        Layout.preferredWidth: 120
                        font.pixelSize: 13
                        text: v("reachedColor") || ""
                        placeholderText: "留空跟随主题"
                        selectByMouse: true
                        onTextChanged: sv("reachedColor", text)
                    }
                    Rectangle {
                        width: 20; height: 20; radius: 3
                        color: {
                            var c = v("reachedColor") || ""
                            return c.length > 0 ? c : "transparent"
                        }
                        border.width: 1
                        border.color: palette.mid
                    }
                }
            }
        }

        // ── 刷新 ──
        GroupBox {
            title: "刷新"
            Layout.fillWidth: true
            RowLayout {
                spacing: 8
                Label { text: "刷新间隔:"; Layout.preferredWidth: 70 }
                SpinBox {
                    id: intervalSpin
                    from: 200; to: 60000
                    stepSize: 500
                    editable: true
                    value: v("intervalMs") !== undefined ? v("intervalMs") : 1000
                    onValueChanged: sv("intervalMs", value)
                }
                Label { text: "ms" }
            }
        }
    }
}
