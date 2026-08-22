// RestReminder.qml — py_rest_reminder 子插件界面
// Bar: 显示状态 + 倒计时。休息时：全屏遮罩从 bar 向下滑落（clip 动画，非 window resize）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root
    property var plugin
    property var pluginContext

    function trText(source) {
        return qsTranslate("plugin.py_intime.main", source)
    }

    width: 130; height: 40

    // 色板（跟随主题，浅色/深色自动适配）
    readonly property var hostColors: pluginContext ? pluginContext.colors
                                                    : (themeManager ? themeManager.colors : ({}))
    readonly property color cFg:      hostColors.fg || "#cdd6f4"
    readonly property color cMuted:   hostColors.muted || "#7f849c"
    readonly property color cSuccess: hostColors.success || "#a6e3a1"
    readonly property color cDanger:  hostColors.danger || "#f38ba8"
    readonly property color cPanel:   hostColors.panel || "#313244"
    readonly property color cBg:      hostColors.bg || "#1e1e2e"
    readonly property color cAccent:  hostColors.accent || "#89b4fa"

    // 进度条样式："none"（不显示）| "bar"（水平）| "ring"（环形），由设置页 progressStyle 控制
    readonly property string progressStyle: (plugin && plugin.values && plugin.values.progressStyle) || "none"
    // 遮罩进入/退出动画时长（ms，0=瞬变），由设置页 enterDurationMs/exitDurationMs 控制
    readonly property int enterDuration: (plugin && plugin.values && plugin.values.enterDurationMs !== undefined)
                                         ? plugin.values.enterDurationMs : 400
    readonly property int exitDuration: (plugin && plugin.values && plugin.values.exitDurationMs !== undefined)
                                        ? plugin.values.exitDurationMs : 400
    // 进度 0..1：total/remaining 由 main.py update() 提供
    readonly property real progress: {
        if (!plugin || !plugin.values) return 0
        var t = Number(plugin.values.total), r = Number(plugin.values.remaining)
        return t > 0 ? Math.min(1, Math.max(0, 1 - r / t)) : 0
    }
    // 平滑进度：update 每秒才拉一次，直接绑 progress 会每秒跳一格；
    // 改为向最新值做 1 秒线性过渡（时长≈拉取间隔），动画即连续细腻
    property real smoothProgress: 0
    onProgressChanged: smoothProgress = progress
    Behavior on smoothProgress { id: progressBehavior; NumberAnimation { duration: 1000; easing.type: Easing.Linear } }
    // 立即归位（不走动画）：阶段切换时工作阶段末尾 progress≈1（满），
    // 若靠动画过渡到休息起点的 0，会看到"满格先回落"的假动作。
    // 注意开关的是 Behavior.enabled（Animation 本身没有 enabled 属性）
    function snapProgress() {
        progressBehavior.enabled = false
        smoothProgress = progress
        progressBehavior.enabled = true
    }

    // ── Bar 显示 ────────────────────────────────────────────────
    Text {
        anchors.centerIn: parent
        color: (plugin && plugin.values && plugin.values.status === "resting")
               ? root.cDanger : root.cFg
        font.pixelSize: 12
        text: {
            if (!plugin || !plugin.values) return root.trText("休息提醒")
            var label = plugin.values.status === "resting"
                      ? root.trText("休息") : root.trText("工作中")
            return label + " " + (plugin.values.timeStr || "--:--")
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (plugin && plugin.values && plugin.values.status === "resting")
                plugin.call("end_rest", [])
        }
    }

    // ── 遮罩（全屏 Window + 内部 clip 动画）─────────────────────
    Window {
        id: overlay
        transientParent: null
        visible: false
        color: "transparent"
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

        property bool _animating: false

        Shortcut {
            sequence: "Escape"
            enabled: overlay.visible && !overlay._animating
            onActivated: {
                if (plugin) plugin.call("end_rest", [])
            }
        }

        Timer {
            interval: 300; repeat: true; running: true
            onTriggered: {
                if (!plugin || !plugin.values) return
                var active = plugin.values.overlayActive || false
                if (active && !overlay.visible && !overlay._animating) {
                    root.showOverlay()
                } else if (!active && overlay.visible && !overlay._animating) {
                    root.hideOverlay()
                }
            }
        }

        onVisibleChanged: {
            if (visible) overlay.requestActivate()
            else clipRect.height = 0
        }

        // ── 裁剪容器：高度从 0 动画到全屏 ────────────────────
        Rectangle {
            id: clipRect
            width: overlay.width; height: 0
            clip: true
            color: "transparent"

            // 遮罩内容（全屏尺寸，被裁剪）
            Rectangle {
                id: maskBg
                width: overlay.width; height: overlay.height
                color: "transparent"

                // 背景
                Loader {
                    anchors.fill: parent
                    sourceComponent: {
                        var mt = plugin && plugin.values ? plugin.values.maskType : "default"
                        if (mt === "image") return imageBg
                        if (mt === "color") return customColorBg
                        return defaultBg
                    }
                }
                Component {
                    id: defaultBg
                    Rectangle {
                        anchors.fill: parent
                        color: root.cBg
                        opacity: (plugin && plugin.values) ? (plugin.values.maskOpacity || 0.92) : 0.92
                    }
                }
                Component {
                    id: customColorBg
                    Rectangle {
                        anchors.fill: parent
                        color: (plugin && plugin.values && plugin.values.maskColor)
                               || root.cBg
                        opacity: (plugin && plugin.values) ? (plugin.values.maskOpacity || 0.92) : 0.92
                    }
                }
                Component {
                    id: imageBg
                    Image {
                        anchors.fill: parent
                        source: (plugin && plugin.values && plugin.values.maskImage) || ""
                        fillMode: Image.PreserveAspectCrop
                        opacity: (plugin && plugin.values) ? (plugin.values.maskOpacity || 0.92) : 0.92
                    }
                }

                // 前景
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 16

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: (plugin && plugin.values) ? (plugin.values.timeStr || "--:--") : "--:--"
                        color: root.cFg
                        font.pixelSize: 72; font.bold: true
                    }

                    // ── 水平进度条（progressStyle === "bar"）──────────
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        visible: root.progressStyle === "bar"
                        width: Math.min(420, overlay.width * 0.5); height: 8; radius: 4
                        color: Qt.rgba(root.cMuted.r, root.cMuted.g, root.cMuted.b, 0.25)
                        Rectangle {
                            height: parent.height; radius: 4
                            width: parent.width * root.smoothProgress
                            color: root.cAccent
                        }
                    }

                    // ── 环形进度条（progressStyle === "ring"）──────────
                    Item {
                        Layout.alignment: Qt.AlignHCenter
                        visible: root.progressStyle === "ring"
                        width: 150; height: 150
                        Canvas {
                            id: ringCanvas
                            anchors.fill: parent
                            // 绑平滑进度：动画期间逐帧重绘（开销可忽略）
                            property real p: root.smoothProgress
                            onPChanged: requestPaint()
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                var cx = width / 2, cy = height / 2
                                var r = Math.min(cx, cy) - 8
                                // 底环（muted 半透明）
                                ctx.lineWidth = 10
                                ctx.globalAlpha = 0.25
                                ctx.strokeStyle = root.cMuted
                                ctx.beginPath()
                                ctx.arc(cx, cy, r, 0, 2 * Math.PI)
                                ctx.stroke()
                                // 进度弧（顶部起，顺时针）
                                ctx.globalAlpha = 1.0
                                ctx.lineCap = "round"
                                ctx.strokeStyle = root.cAccent
                                ctx.beginPath()
                                ctx.arc(cx, cy, r, -Math.PI / 2,
                                        -Math.PI / 2 + 2 * Math.PI * p)
                                ctx.stroke()
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            text: Math.round(root.progress * 100) + "%"
                            color: root.cFg; font.pixelSize: 22; font.bold: true
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: (plugin && plugin.values && plugin.values.maskText)
                              ? plugin.values.maskText : root.trText("休息一下")
                        color: root.cFg
                        font.pixelSize: (plugin && plugin.values) ? (plugin.values.textSize || 24) : 24
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.trText("按 ESC 跳过休息")
                        color: root.cMuted; font.pixelSize: 14
                    }
                }
            }
        }

        // 滑落动画（变更 clipRect.height，而非 Window 尺寸）；
        // duration 由 showOverlay/hideOverlay 在 start 前按进入/退出分别赋值
        NumberAnimation {
            id: slideAnim
            target: clipRect
            property: "height"
            duration: 400
            easing.type: Easing.OutCubic
            from: 0; to: 1080
            onFinished: {
                overlay._animating = false
                if (slideAnim.from > slideAnim.to) {
                    // 收回完成 → 隐藏窗口
                    overlay.hide()
                }
            }
        }
    }

    // ── 辅助 ────────────────────────────────────────────────────
    function showOverlay() {
        if (overlay._animating) return
        // 统一以 barScreenGeometry（bar 所在屏的 availableGeometry，逻辑像素）定位：
        // appbar 模式它已排除 bar 占用的工作区，taskbar 模式它排除任务栏——
        // 两种模式都覆盖除 bar/任务栏外的全屏。
        // 不能以 bar 窗口几何为准：taskbar 模式 bar 是嵌在任务栏里的小窗口，
        // 按它定位遮罩只会盖住任务栏上的一条（本函数此前的 bug）。
        var g = pluginContext ? pluginContext.screenGeometry
                              : ((typeof barScreenGeometry !== "undefined")
                                 ? barScreenGeometry : null)
        var scr = Qt.application.screens[0]
        var gx = g ? g.x : 0
        var gy = g ? g.y : 0
        var gw = g ? g.width  : (scr ? scr.width  : 1920)
        var gh = g ? g.height : (scr ? scr.height : 1080)

        overlay.x = gx
        overlay.y = gy
        overlay.width = gw
        overlay.height = gh

        overlay._animating = true
        snapProgress()   // 开场即归位到休息起点进度（此时 progress 已为 0），防"满格回落"
        clipRect.height = 0
        slideAnim.duration = root.enterDuration
        slideAnim.from = 0
        slideAnim.to = gh
        overlay.show()
        overlay.raise()
        slideAnim.start()
    }

    function hideOverlay() {
        if (overlay._animating) return
        overlay._animating = true
        slideAnim.duration = root.exitDuration
        slideAnim.from = clipRect.height
        slideAnim.to = 0
        slideAnim.start()
    }
}
