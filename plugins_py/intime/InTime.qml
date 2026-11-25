// InTime.qml — 状态栏倒计时与每块屏幕的全屏休息遮罩
pragma ComponentBehavior: Bound
import QtQml.Models
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
    readonly property color cFg:      hostColors.fg || "#ffffff"
    readonly property color cMuted:   hostColors.muted || "#cecece"
    readonly property color cSuccess: hostColors.success || "#6ccb5f"
    readonly property color cDanger:  hostColors.danger || "#ff99a4"
    readonly property color cPanel:   hostColors.panel || "#2b2b2b"
    readonly property color cBg:      hostColors.bg || "#202020"
    readonly property color cAccent:  hostColors.accent || "#60cdff"

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

    readonly property bool overlayActive: !!(plugin && plugin.values && plugin.values.overlayActive)
    onOverlayActiveChanged: if (overlayActive) Qt.callLater(snapProgress)

    // 同一进度驱动每块屏幕的裁剪高度，退出动画结束后一起释放窗口。
    property real reveal: overlayActive ? 1 : 0
    Behavior on reveal {
        NumberAnimation {
            duration: root.overlayActive ? root.enterDuration : root.exitDuration
            easing.type: Easing.OutCubic
        }
    }

    Instantiator {
        objectName: "restOverlays"
        active: root.overlayActive || root.reveal > 0
        model: Qt.application.screens
        delegate: RestOverlay {
            required property var modelData
            screen: modelData
            // 同一虚拟桌面内还需指定窗口原点，副屏坐标可以为负。
            x: modelData.virtualX
            y: modelData.virtualY
            // Windows 软件渲染需要在透明窗口首次全屏显示前取得有效尺寸。
            width: modelData.width
            height: modelData.height
        }
        onObjectAdded: function(index, window) {
            window.showFullScreen()
            window.raise()
            if (index === 0) window.requestActivate()
        }
    }

    component RestOverlay: Window {
        id: overlay
        transientParent: null
        color: "transparent"
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

        Shortcut {
            sequence: "Escape"
            enabled: root.overlayActive
            onActivated: root.plugin.call("end_rest", [])
        }

        // ── 裁剪容器：高度从 0 动画到全屏 ────────────────────
        Rectangle {
            width: overlay.width
            height: overlay.height * root.reveal
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
    }
}
