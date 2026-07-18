// SettingsWindow.qml — 设置窗口（M8）
// 后端：context property settingsVM（草稿属性 + pluginModel + apply/reloadFromConfig +
//       themePackNames/themePackIds 主题包列表）、themeManager（colors 色板驱动 palette）。
// 保存成功或取消都经 Window.window.close() 隐藏（窗口由 SettingsController 持有，不销毁）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root
    padding: 16
    // 圆角 + 裁切：Frameless 透明窗口下，根 Rectangle 的圆角让窗口呈圆角矩形，
    // clip 防止子项（标题栏、滚动内容）溢出圆角区域。
    clip: true
    // 小圆角半径（8px），与内部卡片 radius:6 协调，不过分突兀
    property int cornerRadius: 8

    // 色板别名：统一从 themeManager.colors 取（M9 数据驱动主题）。Basic 样式经
    // palette 继承传给所有子控件，故把 colors 灌进 palette.* 让 QtQuick.Controls
    // 的表单控件也跟随主题。
    readonly property var c: themeManager ? themeManager.colors : ({
        "bg": "#1e1e2e", "panel": "#313244", "fg": "#cdd6f4", "muted": "#7f849c",
        "accent": "#89b4fa", "danger": "#f38ba8", "hover": "#45475a", "border": "#45475a",
        "placeholderText": "#7f849c", "mid": "#7f849c", "dark": "#313244", "light": "#313244"
    })

    background: Rectangle {
        color: root.c.bg
        radius: root.cornerRadius
        // 细边框：Frameless 圆角窗口在浅色背景下加 1px border 增强边界感
        border.width: 1
        border.color: root.c.border
    }
    palette.window: root.c.bg
    palette.windowText: root.c.fg
    palette.base: root.c.bg
    palette.text: root.c.fg
    palette.button: root.c.panel
    palette.buttonText: root.c.fg
    palette.highlight: root.c.accent
    palette.placeholderText: root.c.muted
    palette.mid: root.c.muted
    palette.dark: root.c.panel
    palette.light: root.c.panel

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // ── 自绘标题栏（无边框窗口）───────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: root.c.panel   // 跟随主题，比 bg 略浅，与内容区分层

            Text {
                anchors.centerIn: parent
                text: "玄枢设置 XuanHub Settings"
                color: root.c.fg
                font.pixelSize: 13
                font.bold: true
            }

            // × 关闭按钮（右上角，hover 变 danger 色）
            Item {
                id: closeBtn
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 36; height: 36
                Rectangle {
                    anchors.fill: parent
                    color: closeMa.containsMouse ? root.c.danger : "transparent"
                }
                Text {
                    anchors.centerIn: parent
                    text: "×"
                    color: closeMa.containsMouse ? "#ffffff" : root.c.fg
                    font.pixelSize: 18
                }
                MouseArea {
                    id: closeMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Window.window.close()   // 隐藏窗口（不销毁）
                }
            }

            // 拖动移动窗口：标题栏空白处按下触发系统级窗口移动
            // （避开右侧 × 按钮区域，防误触）
            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: 36
                acceptedButtons: Qt.LeftButton
                onPressed: (mouse) => {
                    if (mouse.button === Qt.LeftButton)
                        Window.window.startSystemMove()   // QWindow::startSystemMove（Win32 SC_MOVE）
                }
            }
        }

        // ── Tab 切换栏 ──────────────────────────────
        // Basic 样式经 palette 继承：选中态用 highlight(accent)、文字用 buttonText(fg)
        TabBar {
            id: tabBar
            Layout.fillWidth: true
            spacing: 0
            TabButton { text: "外观" }
            TabButton { text: "插件" }
            TabButton { text: "通用" }
        }

        // ── Tab 内容（StackLayout 只显示当前页）─────
        StackLayout {
            id: tabStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            // ════════ 外观 Tab ════════
            ScrollView {
                id: appearanceScroll
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    width: appearanceScroll.availableWidth
                    spacing: 14

                    Text { text: "主题"; color: root.c.fg; font.pixelSize: 15; font.bold: true }
                    Rectangle {
                        Layout.fillWidth: true
                        color: root.c.panel
                        radius: 6
                        implicitHeight: themeGrid.implicitHeight + 20

                        GridLayout {
                            id: themeGrid
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 10
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10

                            Text { text: "主题包"; color: root.c.fg; Layout.fillWidth: true }
                            ComboBox {
                                model: settingsVM.themePackNames
                                currentIndex: {
                                    const ids = settingsVM.themePackIds
                                    const i = ids.indexOf(settingsVM.themeId)
                                    return i >= 0 ? i : 0
                                }
                                onActivated: {
                                    const ids = settingsVM.themePackIds
                                    if (index >= 0 && index < ids.length)
                                        settingsVM.themeId = ids[index]
                                }
                            }
                            Text { text: "模式"; color: root.c.fg; Layout.fillWidth: true }
                            ComboBox {
                                model: ["dark", "light"]
                                currentIndex: settingsVM.theme === "light" ? 1 : 0
                                onActivated: settingsVM.theme = currentText
                            }
                        }
                    }

                    Text { text: "状态栏"; color: root.c.fg; font.pixelSize: 15; font.bold: true }
                    Rectangle {
                        Layout.fillWidth: true
                        color: root.c.panel
                        radius: 6
                        implicitHeight: barGrid.implicitHeight + 20

                        GridLayout {
                            id: barGrid
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 10
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10

                            Text { text: "高度（像素）"; color: root.c.fg; Layout.fillWidth: true }
                            SpinBox {
                                from: 24; to: 200
                                value: settingsVM.barHeight
                                onValueModified: settingsVM.barHeight = value
                            }
                            Text { text: "停靠边缘"; color: root.c.fg; Layout.fillWidth: true }
                            ComboBox {
                                model: ["top", "bottom"]
                                currentIndex: settingsVM.barEdge === "bottom" ? 1 : 0
                                onActivated: settingsVM.barEdge = currentText
                            }
                            Text { text: "透明度 (%)"; color: root.c.fg; Layout.fillWidth: true }
                            SpinBox {
                                from: 20; to: 100   // 下限 20 防 bar 完全看不见
                                stepSize: 5
                                value: settingsVM.barOpacity
                                onValueModified: settingsVM.barOpacity = value
                            }
                        }
                    }
                }
            }

            // ════════ 插件 Tab ════════
            ScrollView {
                id: pluginScroll
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    width: pluginScroll.availableWidth
                    spacing: 10

                    Text { text: "插件"; color: root.c.fg; font.pixelSize: 15; font.bold: true }

                    Repeater {
                        model: settingsVM.pluginModel

                        delegate: Rectangle {
                            id: pluginRow
                            Layout.fillWidth: true
                            color: root.c.panel
                            radius: 6
                            implicitHeight: rowCol.implicitHeight + 16

                            property var rowModel: model

                            ColumnLayout {
                                id: rowCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 8
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    CheckBox {
                                        id: enableBox
                                        checked: pluginRow.rowModel.enabled
                                        onToggled: pluginRow.rowModel.enabled = checked
                                    }
                                    ColumnLayout {
                                        spacing: 0
                                        Text {
                                            color: root.c.fg
                                            font.pixelSize: 14
                                            text: pluginRow.rowModel.name ? pluginRow.rowModel.name
                                                                          : pluginRow.rowModel.id
                                        }
                                        Text {
                                            color: root.c.muted
                                            font.pixelSize: 11
                                            text: pluginRow.rowModel.id + " · "
                                                  + (pluginRow.rowModel.kind === "python" ? "Python" : "C++")
                                                  + (pluginRow.rowModel.discovered ? "" : " · 磁盘缺失")
                                        }
                                    }
                                    Item { Layout.fillWidth: true }
                                    ComboBox {
                                        enabled: enableBox.checked
                                        model: ["left", "center", "right"]
                                        currentIndex: Math.max(0, indexOfValue(pluginRow.rowModel.position))
                                        onActivated: pluginRow.rowModel.position = currentText
                                    }
                                    SpinBox {
                                        enabled: enableBox.checked
                                        from: 0; to: 99
                                        value: pluginRow.rowModel.order
                                        onValueModified: pluginRow.rowModel.order = value
                                    }
                                }

                                Button {
                                    visible: enableBox.checked
                                    flat: true
                                    text: cfgArea.visible ? "隐藏 config ▴" : "编辑 config ▾"
                                    onClicked: cfgArea.visible = !cfgArea.visible
                                }
                                TextArea {
                                    id: cfgArea
                                    visible: false
                                    Layout.fillWidth: true
                                    implicitHeight: Math.min(140, contentHeight + 24)
                                    text: pluginRow.rowModel.configJson
                                    onTextChanged: pluginRow.rowModel.configJson = text
                                    font.family: "Consolas"
                                    font.pixelSize: 12
                                    color: root.c.fg
                                    wrapMode: TextEdit.NoWrap
                                    selectByMouse: true
                                    background: Rectangle {
                                        color: root.c.bg
                                        radius: 4
                                        border.width: 1
                                        border.color: cfgArea.activeFocus ? root.c.accent : root.c.muted
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ════════ 通用 Tab ════════
            ScrollView {
                id: generalScroll
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    width: generalScroll.availableWidth
                    spacing: 14

                    Text { text: "系统"; color: root.c.fg; font.pixelSize: 15; font.bold: true }
                    Rectangle {
                        Layout.fillWidth: true
                        color: root.c.panel
                        radius: 6
                        implicitHeight: generalGrid.implicitHeight + 20

                        GridLayout {
                            id: generalGrid
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 10
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10

                            Text { text: "开机自启"; color: root.c.fg; Layout.fillWidth: true }
                            CheckBox {
                                checked: settingsVM.autoStart
                                onToggled: settingsVM.autoStart = checked
                            }
                        }
                    }
                }
            }
        }

        // ── 底部：错误提示 + 按钮（所有 tab 共享）──────
        Text {
            Layout.fillWidth: true
            text: settingsVM.errorMessage
            color: root.c.danger
            wrapMode: Text.Wrap
            visible: settingsVM.errorMessage.length > 0
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }   // 左弹簧，按钮靠右
            Button {
                text: "取消"
                onClicked: Window.window.close()              // 隐藏，不保存
            }
            Button {
                text: "应用"
                enabled: settingsVM.errorMessage.length === 0  // 有错时禁用（保险）
                onClicked: settingsVM.apply()                  // 保存但不关闭
            }
            Button {
                text: "保存"
                highlighted: true
                onClicked: if (settingsVM.apply()) Window.window.close()   // 保存并关闭
            }
        }
    }
}
