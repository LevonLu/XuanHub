// InTimeSettings.qml — InTime 插件自绘设置页
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub   // UiComboBox：裸 ComboBox 的 popup 是独立顶层窗口，palette 继承断裂

Item {
    id: root
    property var plugin
    implicitHeight: col.implicitHeight

    // 读 plugin.revision 建立绑定依赖：value() 是函数调用、自身无通知，
    // 引用 revision 后 setValue 触发的 valuesChanged 会让调用 v() 的绑定重算（预览实时刷新）
    function v(key) {
        if (!plugin) return undefined
        void plugin.revision
        return plugin.value(key)
    }
    function sv(key, val) {
        if (plugin) plugin.setValue(key, val)
    }

    // 延迟初始化（plugin 由 Loader.onLoaded 注入，晚于 Component.onCompleted）
    Timer {
        id: initTimer
        interval: 100; repeat: false
        onTriggered: {
            // 守卫：100ms 内用户可能已切到别的插件，proxy 已改指——
            // 不校验会把 intime 默认值写进新选中插件的草稿
            if (!plugin || plugin.pluginId !== "py_intime") return
            if (v("workSeconds") === undefined) sv("workSeconds", 1500)
            if (v("restSeconds") === undefined) sv("restSeconds", 300)
            if (v("maskType") === undefined) sv("maskType", "default")
            if (v("maskColor") === undefined) sv("maskColor", "#1e1e2e")
            if (v("maskOpacity") === undefined) sv("maskOpacity", 0.92)
            if (v("maskText") === undefined) sv("maskText", "休息一下")
            if (v("textSize") === undefined) sv("textSize", 24)
            if (v("progressStyle") === undefined) sv("progressStyle", "none")
            if (v("enterDurationMs") === undefined) sv("enterDurationMs", 400)
            if (v("exitDurationMs") === undefined) sv("exitDurationMs", 400)
        }
    }
    Component.onCompleted: initTimer.start()

    ColumnLayout {
        id: col
        anchors.left: parent.left; anchors.right: parent.right
        spacing: 12

        // ── 时间 ────────────────────────────────────────────
        UiCard {
            title: "时间"
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 8
                RowLayout {
                    spacing: 8
                    Label { text: "工作时长:"; Layout.preferredWidth: 70 }
                    TextField {
                        Layout.preferredWidth: 80
                        text: v("workSeconds") !== undefined ? String(v("workSeconds")) : "1500"
                        selectByMouse: true
                        validator: IntValidator { bottom: 10; top: 86400 }
                        onTextChanged: { var n = parseInt(text); if (!isNaN(n)) sv("workSeconds", n) }
                    }
                    Label { text: "秒" }
                }
                RowLayout {
                    spacing: 8
                    Label { text: "休息时长:"; Layout.preferredWidth: 70 }
                    TextField {
                        Layout.preferredWidth: 80
                        text: v("restSeconds") !== undefined ? String(v("restSeconds")) : "300"
                        selectByMouse: true
                        validator: IntValidator { bottom: 10; top: 3600 }
                        onTextChanged: { var n = parseInt(text); if (!isNaN(n)) sv("restSeconds", n) }
                    }
                    Label { text: "秒" }
                }
            }
        }

        // ── 动画 ────────────────────────────────────────────
        UiCard {
            title: "动画"
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 8
                RowLayout {
                    spacing: 8
                    Label { text: "进入时长:"; Layout.preferredWidth: 70 }
                    SpinBox {
                        Layout.preferredWidth: 140
                        from: 0; to: 3000; stepSize: 100
                        value: v("enterDurationMs") !== undefined ? v("enterDurationMs") : 400
                        onValueModified: sv("enterDurationMs", value)
                    }
                    Label {
                        text: "ms（0 = 瞬间落下）"
                        color: palette.placeholderText
                        font.pixelSize: 11
                    }
                }
                RowLayout {
                    spacing: 8
                    Label { text: "退出时长:"; Layout.preferredWidth: 70 }
                    SpinBox {
                        Layout.preferredWidth: 140
                        from: 0; to: 3000; stepSize: 100
                        value: v("exitDurationMs") !== undefined ? v("exitDurationMs") : 400
                        onValueModified: sv("exitDurationMs", value)
                    }
                    Label {
                        text: "ms（0 = 瞬间收起）"
                        color: palette.placeholderText
                        font.pixelSize: 11
                    }
                }
            }
        }

        // ── 锁屏进度条 ──────────────────────────────────────
        UiCard {
            title: "锁屏进度条"
            Layout.fillWidth: true
            RowLayout {
                spacing: 8
                UiComboBox {
                    model: ["不显示", "水平进度条", "环形进度条"]
                    currentIndex: {
                        var t = v("progressStyle") || "none"
                        return t === "bar" ? 1 : t === "ring" ? 2 : 0
                    }
                    onCurrentIndexChanged: {
                        var vals = ["none", "bar", "ring"]
                        sv("progressStyle", vals[currentIndex])
                    }
                }
                Label {
                    text: "显示在全屏遮罩倒计时下方"
                    color: palette.placeholderText
                    font.pixelSize: 11
                }
            }
        }

        // ── 遮罩类型 ────────────────────────────────────────
        UiCard {
            title: "遮罩类型"
            Layout.fillWidth: true
            RowLayout {
                spacing: 8
                UiComboBox {
                    id: maskTypeCombo
                    model: ["默认 (跟随主题)", "纯色", "图片"]
                    currentIndex: {
                        var t = v("maskType") || "default"
                        return t === "color" ? 1 : t === "image" ? 2 : 0
                    }
                    onCurrentIndexChanged: {
                        var vals = ["default", "color", "image"]
                        sv("maskType", vals[currentIndex])
                    }
                }
            }
        }

        // ── 遮罩外观（默认模式隐藏颜色/图片设置）─────────────
        UiCard {
            title: "遮罩外观"
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 8
                property bool custom: (v("maskType") || "default") !== "default"

                RowLayout {
                    spacing: 8
                    Label { text: "颜色:"; Layout.preferredWidth: 70 }
                    TextField {
                        Layout.preferredWidth: 100
                        text: v("maskColor") || "#1e1e2e"
                        selectByMouse: true
                        onTextChanged: sv("maskColor", text)
                    }
                    Rectangle {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        radius: 3
                        color: v("maskColor") || "#1e1e2e"
                        border.width: 1; border.color: palette.mid
                    }
                    enabled: parent.custom && v("maskType") !== "image"
                }
                RowLayout {
                    spacing: 8
                    Label { text: "图片:"; Layout.preferredWidth: 70 }
                    TextField {
                        Layout.fillWidth: true
                        text: v("maskImage") || ""
                        placeholderText: "本地图片绝对路径"
                        selectByMouse: true
                        onTextChanged: sv("maskImage", text)
                    }
                    enabled: v("maskType") === "image"
                }
                RowLayout {
                    spacing: 8
                    Label { text: "不透明度:"; Layout.preferredWidth: 70 }
                    Slider {
                        Layout.fillWidth: true
                        from: 0.1; to: 1.0; stepSize: 0.01
                        value: v("maskOpacity") !== undefined ? v("maskOpacity") : 0.92
                        onValueChanged: sv("maskOpacity", value)
                    }
                    Label {
                        text: {
                            var o = v("maskOpacity")
                            return Math.round((o !== undefined ? o : 0.92) * 100) + "%"
                        }
                        Layout.preferredWidth: 35
                    }
                }
                RowLayout {
                    spacing: 8
                    Label { text: "显示文字:"; Layout.preferredWidth: 70 }
                    TextField {
                        Layout.fillWidth: true
                        text: v("maskText") || "休息一下"
                        selectByMouse: true
                        onTextChanged: sv("maskText", text)
                    }
                }
                RowLayout {
                    spacing: 8
                    Label { text: "文字大小:"; Layout.preferredWidth: 70 }
                    SpinBox {
                        Layout.preferredWidth: 80
                        from: 12; to: 72; stepSize: 2
                        value: v("textSize") || 24
                        onValueChanged: sv("textSize", value)
                    }
                }
            }
        }
    }
}
