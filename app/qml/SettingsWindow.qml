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

    // 色板别名：统一从 themeManager.colors 取（M9 数据驱动主题）。Basic 样式经
    // palette 继承传给所有子控件，故把 colors 灌进 palette.* 让 QtQuick.Controls
    // 的表单控件也跟随主题。
    readonly property var c: themeManager ? themeManager.colors : ({
        "bg": "#1e1e2e", "panel": "#313244", "fg": "#cdd6f4", "muted": "#7f849c",
        "accent": "#89b4fa", "danger": "#f38ba8", "hover": "#45475a", "border": "#45475a",
        "placeholderText": "#7f849c", "mid": "#7f849c", "dark": "#313244", "light": "#313244"
    })

    background: Rectangle { color: root.c.bg }
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

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: scroll.availableWidth
                spacing: 14

                // ── 状态栏设置 ───────────────────────────
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
                        Text { text: "主题包"; color: root.c.fg; Layout.fillWidth: true }
                        ComboBox {
                            // 主题包下拉（names 显示，ids 映射回 themeId 保存）
                            model: settingsVM.themePackNames
                            // 选中当前 themeId 对应的 name
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
                        Text { text: "开机自启"; color: root.c.fg; Layout.fillWidth: true }
                        CheckBox {
                            checked: settingsVM.autoStart
                            onToggled: settingsVM.autoStart = checked
                        }
                    }
                }

                // ── 插件设置 ───────────────────────────
                Text { text: "插件"; color: root.c.fg; font.pixelSize: 15; font.bold: true }

                Repeater {
                    model: settingsVM.pluginModel

                    delegate: Rectangle {
                        id: pluginRow
                        Layout.fillWidth: true
                        color: root.c.panel
                        radius: 6
                        implicitHeight: rowCol.implicitHeight + 16

                        // 显式持有委托上下文的 model：ComboBox 等控件自身的 model 属性
                        // 会遮蔽角色名访问，经 rowModel.xxx 读写可避开（赋值走 setData）
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

        // ── 底部：错误提示 + 按钮 ───────────────────────
        Text {
            Layout.fillWidth: true
            text: settingsVM.errorMessage
            color: root.c.danger
            wrapMode: Text.Wrap
            visible: settingsVM.errorMessage.length > 0
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: "取消"
                onClicked: Window.window.close()
            }
            Button {
                text: "保存"
                highlighted: true
                onClicked: if (settingsVM.apply()) Window.window.close()
            }
        }
    }
}
