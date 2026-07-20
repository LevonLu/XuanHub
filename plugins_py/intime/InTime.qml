// RestReminder.qml — py_rest_reminder 子插件界面
// Bar: 显示状态 + 倒计时。休息时：全屏遮罩从 bar 向下滑落（clip 动画，非 window resize）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root
    property var plugin

    width: 130; height: 40

    // 色板（跟随主题，浅色/深色自动适配）
    readonly property color cFg:      themeManager ? themeManager.colors.fg : "#cdd6f4"
    readonly property color cMuted:   themeManager ? themeManager.colors.muted : "#7f849c"
    readonly property color cSuccess: themeManager ? themeManager.colors.success : "#a6e3a1"
    readonly property color cDanger:  themeManager ? themeManager.colors.danger : "#f38ba8"
    readonly property color cPanel:   themeManager ? themeManager.colors.panel : "#313244"
    readonly property color cBg:      themeManager ? themeManager.colors.bg : "#1e1e2e"

    // ── Bar 显示 ────────────────────────────────────────────────
    Text {
        anchors.centerIn: parent
        color: (plugin && plugin.values && plugin.values.status === "resting")
               ? root.cDanger : root.cFg
        font.pixelSize: 12
        text: {
            if (!plugin || !plugin.values) return "休息提醒"
            return (plugin.values.label || "工作中") + " " + (plugin.values.timeStr || "--:--")
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
                        color: themeManager ? themeManager.colors.bg : "#1e1e2e"
                        opacity: (plugin && plugin.values) ? (plugin.values.maskOpacity || 0.92) : 0.92
                    }
                }
                Component {
                    id: customColorBg
                    Rectangle {
                        anchors.fill: parent
                        color: (plugin && plugin.values && plugin.values.maskColor)
                               || (themeManager ? themeManager.colors.bg : "#1e1e2e")
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
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: (plugin && plugin.values) ? (plugin.values.maskText || "休息一下") : "休息一下"
                        color: root.cFg
                        font.pixelSize: (plugin && plugin.values) ? (plugin.values.textSize || 24) : 24
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "按 ESC 跳过休息"
                        color: root.cMuted; font.pixelSize: 14
                    }
                }
            }
        }

        // 滑落动画（变更 clipRect.height，而非 Window 尺寸）
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
        const bw = root.Window.window
        if (!bw || overlay._animating) return
        const atBottom = (typeof barConfig !== "undefined") && barConfig.barEdge === "bottom"
        const scr = Qt.application.screens[0]
        const sh = scr ? scr.height : 1080

        overlay.x = bw.x
        overlay.width = bw.width
        overlay.height = sh
        if (atBottom) overlay.y = 0
        else overlay.y = bw.y + bw.height

        overlay._animating = true
        clipRect.height = 0
        slideAnim.from = 0
        slideAnim.to = sh - (atBottom ? 0 : bw.y + bw.height)
        overlay.show()
        overlay.raise()
        slideAnim.start()
    }

    function hideOverlay() {
        if (overlay._animating) return
        overlay._animating = true
        slideAnim.from = clipRect.height
        slideAnim.to = 0
        slideAnim.start()
    }
}
