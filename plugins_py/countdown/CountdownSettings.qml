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
    function trText(source) { return qsTranslate("plugin.py_countdown.settingsPage", source) }
    implicitHeight: col.implicitHeight + 16
    implicitWidth: col.implicitWidth

    // 初始化守卫：防 TextField 初始化时触发 commit 回写循环
    property bool _loading: false

    // 读 plugin.revision 建立绑定依赖：value() 是函数调用、自身无通知，
    // 引用 revision 后 setValue 触发的 valuesChanged 会让调用 v() 的绑定重算（预览实时刷新）
    function v(key) { if (!plugin) return undefined; void plugin.revision; return plugin.value(key) }
    function sv(key, val) {
        if (plugin && plugin.pluginId === "py_countdown") plugin.setValue(key, val)
    }

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

    property string currentMode: "daily"
    property bool isDaily: currentMode === "daily"

    function normalizedMode(value) {
        return value === "date" || value === "once" || value === "指定日期"
                ? "date" : "daily"
    }

    // ── plugin 注入后初始化默认值 ──
    // 守卫：proxy 必须已指向本插件（防框架切换时序/异步路径把默认值写进别的插件）
    onPluginChanged: {
        if (!plugin || plugin.pluginId !== "py_countdown") return
        _loading = true

        var mode = normalizedMode(v("mode"))
        if (v("mode") !== mode) sv("mode", mode)
        root.currentMode = mode

        if (v("targetTime") === undefined) sv("targetTime", "18:00")
        if (v("targetDateTime") === undefined) sv("targetDateTime", "")
        if (v("prefix") === undefined) sv("prefix", trText("下班"))
        if (v("suffix") === undefined) sv("suffix", trText("剩余"))
        if (v("reachedText") === undefined) sv("reachedText", trText("下班啦🎉"))
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
        UiCard {
            title: root.trText("目标模式")
            Layout.fillWidth: true
            RowLayout {
                spacing: 8
                Label { text: root.trText("模式:"); Layout.preferredWidth: 70 }
                UiComboBox {
                    id: modeCombo
                    model: [
                        { value: "daily", label: root.trText("每日重复") },
                        { value: "date", label: root.trText("指定日期") }
                    ]
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: root.isDaily ? 0 : 1
                    onActivated: {
                        if (currentIndex < 0) return
                        sv("mode", currentValue)
                        root.currentMode = currentValue
                        root.reloadTimeFields()
                    }
                }
            }
        }

        // ── 目标时间 ──
        UiCard {
            title: root.trText("目标时间")
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 8

                // 每日模式：时 + 分 两个输入框
                RowLayout {
                    visible: root.isDaily
                    spacing: 6
                    Label { text: root.trText("每日时刻:"); Layout.preferredWidth: 70 }
                    TextField {
                        id: hoursField
                        Layout.preferredWidth: 50
                        font.pixelSize: 13
                        horizontalAlignment: TextInput.AlignHCenter
                        selectByMouse: true
                        validator: IntValidator { bottom: 0; top: 23 }
                        onTextChanged: root.commitTargetTime()
                    }
                    Label { text: root.trText("时") }
                    TextField {
                        id: minutesField
                        Layout.preferredWidth: 50
                        font.pixelSize: 13
                        horizontalAlignment: TextInput.AlignHCenter
                        selectByMouse: true
                        validator: IntValidator { bottom: 0; top: 59 }
                        onTextChanged: root.commitTargetTime()
                    }
                    Label { text: root.trText("分") }
                }

                // 指定日期模式：日期 + 时:分
                ColumnLayout {
                    visible: !root.isDaily
                    spacing: 6
                    RowLayout {
                        spacing: 6
                        Label { text: root.trText("目标日期:"); Layout.preferredWidth: 70 }
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
                        Label { text: root.trText("目标时刻:"); Layout.preferredWidth: 70 }
                        TextField {
                            id: dtHoursField
                            Layout.preferredWidth: 50
                            font.pixelSize: 13
                            horizontalAlignment: TextInput.AlignHCenter
                            selectByMouse: true
                            validator: IntValidator { bottom: 0; top: 23 }
                            onTextChanged: root.commitTargetDateTime()
                        }
                        Label { text: root.trText("时") }
                        TextField {
                            id: dtMinutesField
                            Layout.preferredWidth: 50
                            font.pixelSize: 13
                            horizontalAlignment: TextInput.AlignHCenter
                            selectByMouse: true
                            validator: IntValidator { bottom: 0; top: 59 }
                            onTextChanged: root.commitTargetDateTime()
                        }
                        Label { text: root.trText("分") }
                    }
                }
            }
        }

        // ── 显示 ──
        UiCard {
            title: root.trText("显示")
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 8

                RowLayout {
                    spacing: 8
                    Label { text: root.trText("预览:"); Layout.preferredWidth: 70 }
                    Label {
                        text: {
                            var p = v("prefix") || ""
                            var s = v("suffix") || ""
                            var t = root.isDaily ? root.previewTime() : "02:30:15"
                            var parts = [p, t, s].filter(function(x) { return x.length > 0 })
                            return parts.join(" ") || root.trText("（空）")
                        }
                        color: palette.mid
                        font.italic: true
                    }
                }

                RowLayout {
                    spacing: 8
                    Label { text: root.trText("前缀:"); Layout.preferredWidth: 70 }
                    TextField {
                        Layout.fillWidth: true
                        font.pixelSize: 13
                        text: v("prefix") || ""
                        placeholderText: root.trText("倒计时前的文字，留空则不显示前缀")
                        selectByMouse: true
                        onTextChanged: if (!root._loading && v("prefix") !== text) sv("prefix", text)
                    }
                }
                RowLayout {
                    spacing: 8
                    Label { text: root.trText("后缀:"); Layout.preferredWidth: 70 }
                    TextField {
                        Layout.fillWidth: true
                        font.pixelSize: 13
                        text: v("suffix") || ""
                        placeholderText: root.trText("倒计时后的文字，留空则不显示后缀")
                        selectByMouse: true
                        onTextChanged: if (!root._loading && v("suffix") !== text) sv("suffix", text)
                    }
                }
                RowLayout {
                    spacing: 8
                    Label { text: root.trText("到点文字:"); Layout.preferredWidth: 70 }
                    TextField {
                        Layout.fillWidth: true
                        font.pixelSize: 13
                        text: v("reachedText") || root.trText("下班啦🎉")
                        placeholderText: root.trText("倒计时归零后显示")
                        selectByMouse: true
                        onTextChanged: if (!root._loading && v("reachedText") !== text) sv("reachedText", text)
                    }
                }
                RowLayout {
                    spacing: 8
                    Label { text: root.trText("到点颜色:"); Layout.preferredWidth: 70 }
                    TextField {
                        Layout.preferredWidth: 120
                        font.pixelSize: 13
                        text: v("reachedColor") || ""
                        placeholderText: root.trText("留空跟随主题")
                        selectByMouse: true
                        onTextChanged: if (!root._loading && v("reachedColor") !== text) sv("reachedColor", text)
                    }
                    Rectangle {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        radius: 3
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

        // ── 悬浮提示 ──
        UiCard {
            title: root.trText("悬浮提示")
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 8
                Label {
                    text: root.trText("鼠标停留时显示，留空关闭。支持多行；{payday} 为下次发薪日期，{paydayDays} 为剩余天数。")
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                TextArea {
                    objectName: "countdownTooltipEditor"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 88
                    text: v("tooltipText") || ""
                    placeholderText: root.trText("输入提示信息，或点击下方按钮填入发薪日示例")
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    onTextChanged: if (!root._loading && v("tooltipText") !== text) sv("tooltipText", text)
                }
                Button {
                    objectName: "countdownPaydayExample"
                    text: root.trText("填入发薪日示例")
                    onClicked: sv("tooltipText", root.trText("下次发工资：{payday}（还有 {paydayDays} 天）"))
                }
                RowLayout {
                    spacing: 8
                    Label { text: root.trText("每月发薪日:") }
                    SpinBox {
                        from: 1; to: 31
                        editable: true
                        value: v("paydayDay") !== undefined ? v("paydayDay") : 10
                        onValueModified: sv("paydayDay", value)
                    }
                    Label { text: root.trText("号") }
                }
                Label {
                    text: root.trText("按中国大陆节假日和调休补班提前到最近工作日；当天仍显示本次发薪。短月取月末。内置 2026 年日历，未收录年份会提示待确认。")
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: palette.mid
                }
                Label { text: root.trText("额外休息日（公司安排，可留空）") }
                TextField {
                    Layout.fillWidth: true
                    text: v("paydayHolidays") || ""
                    placeholderText: root.trText("YYYY-MM-DD，多个日期用逗号分隔")
                    selectByMouse: true
                    onTextEdited: sv("paydayHolidays", text)
                }
                Label { text: root.trText("额外工作日（公司安排，可留空）") }
                TextField {
                    Layout.fillWidth: true
                    text: v("paydayWorkdays") || ""
                    placeholderText: root.trText("YYYY-MM-DD，多个日期用逗号分隔")
                    selectByMouse: true
                    onTextEdited: sv("paydayWorkdays", text)
                }
            }
        }

        // ── 刷新 ──
        UiCard {
            title: root.trText("刷新")
            Layout.fillWidth: true
            RowLayout {
                spacing: 8
                Label { text: root.trText("刷新间隔:"); Layout.preferredWidth: 70 }
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
