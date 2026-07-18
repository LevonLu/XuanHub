// Clock.qml — 时钟显示 + 点击弹出日历气泡窗口。契约：根 Item 声明 property var plugin。
// 设计取舍：日历用独立顶层 Window（非 Popup）——宿主 Bar 仅 40px 高，
// Popup 在 overlay 层渲染会被窗口几何裁切；Window 是独立 native 窗口，不受此限。
// 配色跟随宿主根上下文的 context property themeManager.colors（同引擎可见）。
//
// MonthGrid/DayOfWeekRow/WeekNumberColumn 属 QtQuick.Controls（Qt6.2+），由 Basic 样式实现：
//   - MonthGrid.month/year 是 int（0-11 / 公元年），不是 Date
//   - MonthGrid delegate 的 model 角色：date(QDateTime) day(int) month(int) year(int)
//                                       today(bool,Qt 内置今日标记) weekNumber(int)
//   - DayOfWeekRow delegate：shortName / narrowName / longName（required property）
//   - WeekNumberColumn delegate：weekNumber（required property int）
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root
    property var plugin

    width: 130
    height: 40

    // 注意：不用 Screen attached 获取屏幕几何。在 Loader 动态加载的插件组件中，
    // Screen.virtualGeometry 会在组件实例化时（尚未挂到窗口 scene graph）求值为
    // undefined，且绑定会因此丢失（Qt 已知行为）。改用「先 show 再定位」两步法：
    // 日历 Window show 后会挂到某个 screen，其内部 Item 的 Screen attached 才有效。

    // 色板别名：统一从 themeManager.colors 取（M9 数据驱动主题），缺失时给深色兜底
    readonly property var colors: themeManager ? themeManager.colors : ({
        "bg": "#1e1e2e", "panel": "#313244", "fg": "#cdd6f4", "muted": "#7f849c",
        "accent": "#89b4fa", "danger": "#f38ba8", "success": "#a6e3a1", "warning": "#f9e2af",
        "hover": "#45475a", "border": "#45475a", "onAccent": "#1e1e2e"
    })

    Text {
        anchors.centerIn: parent
        color: root.colors.fg
        font.pixelSize: 16
        text: plugin ? plugin.timeText : "--:--:--"
    }

    // 左键：切换日历显隐；右键：保留原日志（M4 交互验证遗留）
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        onClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                if (calendarWin.visible) calendarWin.hide()
                else root.openCalendar()
            } else {
                console.log("[Clock] right-clicked")
            }
        }
    }

    // 弹出日历：以时钟中心点为锚，日历水平居中于时钟下方弹出（气泡感）。
    // 采用「先 show 再定位」两步法：Window show 后挂到 screen，其内 Item 的
    // Screen attached 才有效（Loader 加载的插件组件中，root 上的 Screen 是 undefined）。
    // 初次定位用时钟坐标做粗略居中，show 后在 onAfterAnimating 用真实屏幕几何修正越界。
    function openCalendar() {
        const w = calendarWin.width
        const h = calendarWin.height
        // 锚点取时钟水平中心、下沿 +6px 间隙
        const belowPt = root.mapToGlobal(Qt.point(root.width / 2, root.height + 6))

        // 日历水平居中于时钟中心，垂直贴时钟下沿
        calendarWin.x = Math.round(belowPt.x - w / 2)
        calendarWin.y = Math.round(belowPt.y)

        // 回到今天所在月（同时设置 month + year 两个 int 属性）
        const now = new Date()
        calendarWin.viewYear = now.getFullYear()
        calendarWin.viewMonth = now.getMonth()

        calendarWin.show()           // 显示后窗口挂到 screen，attached 才生效
        calendarWin.raise()
        calendarWin.requestActivate()
        calendarWin.pendingClamp = true   // 标记：等首个 frame 后做越界修正
    }

    // ── 日历气泡窗口 ──────────────────────────────────────────
    Window {
        id: calendarWin
        // 当前显示月/年（int，MonthGrid.month/year 直接绑定）
        property int viewMonth: 0
        property int viewYear: 0
        property bool pendingClamp: false   // show 后等待首个 frame 做越界修正

        visible: false
        width: 320
        height: 360
        color: root.colors.bg
        // Tool：不进任务栏/Alt+Tab；Frameless：无系统标题栏（自绘）；
        // StaysOnTop：与 Bar 同层级置顶；不加 NoFocus：日历需接收 Esc 与悬停高亮
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

        // 失活（点击窗口外）自动关闭——常见气泡行为
        onActiveChanged: if (!active && visible) hide()

        // 窗口 show 后挂到 screen，此时内部 Item 的 Screen attached 才有效。
        // 用 Qt.callLater 在 show() 返回、窗口挂到 screen 后做一次越界修正。
        onVisibleChanged: if (visible && pendingClamp) Qt.callLater(doClamp)

        // Esc 关闭（窗口作用域 Shortcut）
        Shortcut {
            sequence: "Escape"
            onActivated: calendarWin.hide()
        }

        // 屏幕几何探针：Window 内部的 Item 才能正确拿到 Screen attached
        //（Window show 后挂到 screen，attached 生效）。不可见不影响功能。
        Item {
            id: screenProbe
            visible: false
        }

        // 越界修正：show 后用真实屏幕几何把日历拉回可视区，必要时上翻
        function doClamp() {
            calendarWin.pendingClamp = false
            const sg = screenProbe.Screen.virtualGeometry  // 现在有效
            if (!sg || sg.width === 0) return              // 保险：拿不到就不修正
            const w = calendarWin.width
            const h = calendarWin.height
            let x = calendarWin.x
            let y = calendarWin.y
            // 水平越界收边
            if (x + w > sg.right)  x = Math.round(sg.right - w)
            if (x < sg.left)       x = sg.left
            // 垂直下越界 → 改从时钟上方弹出（重新算 y）
            if (y + h > sg.bottom) {
                const abovePt = root.mapToGlobal(Qt.point(root.width / 2, -6))
                y = Math.round(abovePt.y - h)
            }
            if (y < sg.top) y = sg.top
            calendarWin.x = x
            calendarWin.y = y
        }

        // 中文 locale；系统无 zh_CN 时 Qt 自动回退，不报错
        readonly property var loc: Qt.locale("zh_CN")

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            // ── 标题行：‹  yyyy 年 M 月  › ───────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Item { Layout.fillWidth: true }  // 左弹簧，标题居中

                // 上一月（自绘按钮：避免 RoundButton + flat + 自定义 background 的不确定性）
                Item {
                    implicitWidth: 34; implicitHeight: 34
                    Layout.alignment: Qt.AlignVCenter
                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: prevMa.containsMouse ? root.colors.hover : "transparent"
                        border.width: prevMa.containsMouse ? 1 : 0
                        border.color: root.colors.muted
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "◀"
                        color: root.colors.fg
                        font.pixelSize: 14
                    }
                    MouseArea {
                        id: prevMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            console.log("[Clock] prev month clicked")
                            calendarWin.shiftMonth(-1)
                        }
                    }
                    ToolTip.visible: prevMa.containsMouse
                    ToolTip.delay: 500
                    ToolTip.text: "上一月"
                }
                Text {
                    Layout.minimumWidth: 130
                    horizontalAlignment: Text.AlignHCenter
                    color: root.colors.fg
                    font.pixelSize: 15
                    font.bold: true
                    text: calendarWin.viewYear + " 年 "
                          + (calendarWin.viewMonth + 1) + " 月"
                }
                // 下一月（自绘按钮）
                Item {
                    implicitWidth: 34; implicitHeight: 34
                    Layout.alignment: Qt.AlignVCenter
                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: nextMa.containsMouse ? root.colors.hover : "transparent"
                        border.width: nextMa.containsMouse ? 1 : 0
                        border.color: root.colors.muted
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "▶"
                        color: root.colors.fg
                        font.pixelSize: 14
                    }
                    MouseArea {
                        id: nextMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            console.log("[Clock] next month clicked")
                            calendarWin.shiftMonth(1)
                        }
                    }
                    ToolTip.visible: nextMa.containsMouse
                    ToolTip.delay: 500
                    ToolTip.text: "下一月"
                }

                Item { Layout.fillWidth: true }  // 右弹簧
            }

            // ── "回到今天"按钮：偏离当月时显示 ──────────────────
            Item {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredHeight: 26
                Layout.preferredWidth: todayRow.implicitWidth + 20
                visible: !calendarWin.isViewingThisMonth()
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: todayMa.containsMouse ? root.colors.hover : "transparent"
                    border.width: 1
                    border.color: root.colors.accent
                }
                Text {
                    id: todayRow
                    anchors.centerIn: parent
                    text: "回到今天"
                    color: root.colors.accent
                    font.pixelSize: 12
                }
                MouseArea {
                    id: todayMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        console.log("[Clock] back to today clicked")
                        const now = new Date()
                        calendarWin.viewYear = now.getFullYear()
                        calendarWin.viewMonth = now.getMonth()
                    }
                }
            }

            // ── 月历主体：周数列 | (星期行 + 月历网格) ────────
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 4

                // 周数列
                WeekNumberColumn {
                    month: calendarWin.viewMonth
                    year: calendarWin.viewYear
                    locale: calendarWin.loc
                    Layout.fillHeight: true
                    Layout.preferredWidth: 28
                    delegate: Text {
                        required property int weekNumber
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: root.colors.muted
                        font.pixelSize: 11
                        text: weekNumber
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 2

                    // 星期表头（一二三四五六日）
                    DayOfWeekRow {
                        locale: calendarWin.loc
                        Layout.fillWidth: true
                        Layout.preferredHeight: 22
                        delegate: Text {
                            required property string shortName
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: root.colors.muted
                            font.pixelSize: 12
                            font.bold: true
                            text: shortName
                        }
                    }

                    // 月历网格
                    MonthGrid {
                        id: grid
                        month: calendarWin.viewMonth
                        year: calendarWin.viewYear
                        locale: calendarWin.loc
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 2

                        // Qt 内置 today 标记判断今日；model.month != grid.month 判非当月溢出格
                        delegate: Rectangle {
                            required property var model
                            readonly property bool isToday: model.today
                            readonly property bool isOutside: model.month !== grid.month

                            color: isToday ? root.colors.accent
                                  : (ma.containsMouse ? root.colors.hover : "transparent")
                            radius: 6

                            Text {
                                anchors.centerIn: parent
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                color: parent.isToday ? root.colors.onAccent
                                      : (parent.isOutside ? root.colors.muted : root.colors.fg)
                                font.pixelSize: 12
                                font.bold: parent.isToday
                                text: parent.model.day
                            }

                            MouseArea {
                                id: ma
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: console.log("[Clock] picked:", parent.model.date)
                            }
                        }

                        // 点击日期信号（MonthGrid 自带 clicked(QDateTime)）
                        onClicked: (date) => console.log("[Clock] grid clicked:", date)
                    }
                }
            }

            // ── 底部：今日完整日期 ─────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                color: root.colors.panel
                radius: 6
                Text {
                    anchors.centerIn: parent
                    color: root.colors.fg
                    font.pixelSize: 12
                    // ISO 日期 + 中文周几（dayName 用 today 的星期几）
                    text: {
                        const t = new Date()
                        return Qt.formatDate(t, "yyyy-MM-dd") + "  "
                             + calendarWin.loc.dayName(t.getDay(), Locale.ShortFormat)
                    }
                }
            }
        }

        // 切月：viewMonth ±1，越界（<0 或 >11）时进/退位年份
        function shiftMonth(delta) {
            console.log("[Clock] shiftMonth delta=" + delta
                        + " from " + viewYear + "-" + (viewMonth + 1))
            let m = viewMonth + delta
            let y = viewYear
            if (m < 0)  { m = 11; y -= 1 }
            if (m > 11) { m = 0;  y += 1 }
            viewYear = y
            viewMonth = m
        }

        // 判断当前查看月是否就是今天所在月（用于"回到今天"按钮的显隐）
        function isViewingThisMonth() {
            const now = new Date()
            return calendarWin.viewYear === now.getFullYear()
                && calendarWin.viewMonth === now.getMonth()
        }
    }
}
