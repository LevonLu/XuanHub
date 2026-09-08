// Clock.qml — 时钟显示 + 点击弹出日历气泡窗口。契约：根 Item 声明 property var plugin。
// 日历使用宿主 PluginPopup：仍是独立顶层 Window，不受状态栏窗口几何裁切，
// 但定位、跨插件互斥、Esc/失焦/bar 点击关闭由宿主统一处理。
// 配色、语言、屏幕范围和 bar 事件优先通过插件作用域 pluginContext 获取。
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
import XuanHub

Item {
    id: root
    property var plugin
    property var pluginContext

    // 宽度跟随内容：format 可配（如 yyyy-MM-dd HH:mm:ss），写死宽度会被槽位
    // clip:true 静默截断；留 24px 水平内边距保持视觉留白
    width: timeText.implicitWidth + 24
    height: 40

    // 注意：不用 Screen attached 取屏幕几何。弹窗先开在屏幕外时其窗口未挂到任何
    // screen，内部 Item 的 Screen 无效；taskbar 模式下 bar 是任务栏子窗口，
    // root.Screen 也会失效（实测 undefined）。统一用 pluginContext.screenGeometry
    // （BarWindow::setupBar 更新）+ mapToGlobal 真实屏幕坐标。

    // 所有宿主服务只依赖带版本的作用域上下文。
    readonly property var colors: pluginContext ? pluginContext.colors
                                                : ({
        "bg": "#1e1e2e", "panel": "#313244", "fg": "#cdd6f4", "muted": "#7f849c",
        "accent": "#89b4fa", "danger": "#f38ba8", "success": "#a6e3a1", "warning": "#f9e2af",
        "hover": "#45475a", "border": "#45475a", "onAccent": "#1e1e2e"
    })

    // bar hover 反馈（提示可点击弹日历）；barMa 在下方声明，运行时解析无碍
    property color hoverColor: root.colors.hover
    Rectangle {
        anchors.fill: parent
        radius: 6
        color: Qt.rgba(root.hoverColor.r, root.hoverColor.g, root.hoverColor.b,
                       barMa.containsMouse ? 0.35 : 0)
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    Text {
        id: timeText
        anchors.centerIn: parent
        color: root.colors.fg
        font.pixelSize: 16
        text: plugin ? plugin.timeText : "--:--:--"
    }

    // 左键：切换日历显隐；右键：保留原日志（M4 交互验证遗留）
    MouseArea {
        id: barMa
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        onClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                root.openCalendar()
            } else {
                console.log("[Clock] right-clicked")
            }
        }
    }

    // 打开前回到今天所在月；定位和 toggle 守卫交给 PluginPopup。
    function openCalendar() {
        const now = new Date()
        calendarWin.viewYear = now.getFullYear()
        calendarWin.viewMonth = now.getMonth()
        calendarWin.toggleFromBar()
    }

    // ── 日历气泡窗口 ──────────────────────────────────────────
    PluginPopup {
        id: calendarWin
        anchorItem: root
        pluginContext: root.pluginContext
        // 当前显示月/年（int，MonthGrid.month/year 直接绑定）
        property int viewMonth: 0
        property int viewYear: 0

        width: 320
        height: 360

        // 圆角 + 边框：frameless 窗口自身不能圆角，透明窗口 + 底层 Rectangle 实现
        //（与用量中心弹窗同一范式）；ColumnLayout 叠在其上，z 序天然靠后声明者在上
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            color: root.colors.bg
            radius: 8
            border.width: 1
            border.color: root.colors.border
        }

        // 统一使用宿主解析后的活动语言；system 已由 LocaleManager 归一为中/英文。
        readonly property var loc: Qt.locale(pluginContext ? pluginContext.activeLocale
                                                           : "zh-CN")

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
                    ToolTip.text: qsTranslate("plugin.clock.calendar", "上一月")
                }
                Text {
                    Layout.minimumWidth: 130
                    horizontalAlignment: Text.AlignHCenter
                    color: root.colors.fg
                    font.pixelSize: 15
                    font.bold: true
                    text: qsTranslate("plugin.clock.calendar", "%1 年 %2 月")
                              .arg(calendarWin.viewYear)
                              .arg(calendarWin.viewMonth + 1)
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
                    ToolTip.text: qsTranslate("plugin.clock.calendar", "下一月")
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
                    text: qsTranslate("plugin.clock.calendar", "回到今天")
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
