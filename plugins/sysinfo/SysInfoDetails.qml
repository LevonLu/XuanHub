// SysInfoDetails.qml — 系统信息详情弹窗的四卡片仪表盘
import QtQuick
import QtQuick.Layouts

Item {
    id: root
    property var plugin
    property var pluginContext
    signal closeRequested()

    function trText(source) { return qsTranslate("plugin.sysinfo.main", source) }
    function colors() {
        if (pluginContext && pluginContext.colors) return pluginContext.colors
        if (typeof themeManager !== "undefined" && themeManager) return themeManager.colors
        return {
            bg: "#1e1e2e", panel: "#313244", fg: "#cdd6f4", muted: "#7f849c",
            accent: "#89b4fa", success: "#a6e3a1", warning: "#f9e2af",
            danger: "#f38ba8", hover: "#45475a", border: "#45475a"
        }
    }

    readonly property var paletteData: colors()
    readonly property color cBg: paletteData.bg
    readonly property color cPanel: paletteData.panel
    readonly property color cFg: paletteData.fg
    readonly property color cMuted: paletteData.muted
    readonly property color cAccent: paletteData.accent
    readonly property color cSuccess: paletteData.success
    readonly property color cWarning: paletteData.warning
    readonly property color cDanger: paletteData.danger
    readonly property color cHover: paletteData.hover
    readonly property color cBorder: paletteData.border

    component StatTile: Rectangle {
        property string caption: ""
        property string value: "---"
        property color foreground: root.cFg
        color: Qt.rgba(root.cHover.r, root.cHover.g, root.cHover.b, 0.46)
        border.width: 1
        border.color: Qt.rgba(root.cBorder.r, root.cBorder.g, root.cBorder.b, 0.72)
        radius: 7
        implicitHeight: 44

        Column {
            anchors.centerIn: parent
            width: parent.width - 10
            spacing: 2
            Text {
                width: parent.width
                text: caption
                color: root.cMuted
                font.pixelSize: 10
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: value
                color: foreground
                font.pixelSize: 13
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        color: root.cBg
        radius: 10
        border.width: 1
        border.color: root.cBorder

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: root.trText("系统信息")
                    color: root.cFg
                    font.pixelSize: 16
                    font.weight: Font.Bold
                }
                Text {
                    text: root.trText("最近 60 次采样")
                    color: root.cMuted
                    font.pixelSize: 10
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: "×"
                    color: closeArea.containsMouse ? root.cFg : root.cMuted
                    font.pixelSize: 20
                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        anchors.margins: -8
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closeRequested()
                    }
                }
            }

            Flickable {
                id: viewport
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: cards.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                GridLayout {
                    id: cards
                    width: viewport.width
                    columns: width >= 680 ? 2 : 1
                    columnSpacing: 10
                    rowSpacing: 10

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredWidth: cards.columns === 2 ? (cards.width - 10) / 2 : cards.width
                        Layout.preferredHeight: 250
                        color: root.cPanel; radius: 9
                        border.width: 1; border.color: root.cBorder

                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 12; spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                SysInfoIcon { width: 18; height: 18; metric: "cpu"; iconColor: root.cAccent }
                                Text { text: root.trText("CPU 使用率"); color: root.cFg; font.pixelSize: 14; font.weight: Font.Bold }
                                Item { Layout.fillWidth: true }
                                Text { text: root.plugin ? root.plugin.cpuText : "---"; color: root.cAccent; font.pixelSize: 18; font.weight: Font.Bold }
                            }
                            Text { text: root.trText("利用率"); color: root.cMuted; font.pixelSize: 10 }
                            SysInfoChart {
                                Layout.fillWidth: true; Layout.preferredHeight: 92
                                primarySeries: root.plugin ? root.plugin.cpuHistory : []
                                primaryColor: root.cAccent; gridColor: root.cBorder; maximum: 100
                            }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 7
                                StatTile { Layout.fillWidth: true; caption: root.trText("当前频率"); value: root.plugin ? root.plugin.cpuCurrentFrequencyText : "---" }
                                StatTile { Layout.fillWidth: true; caption: root.trText("最高频率"); value: root.plugin ? root.plugin.cpuMaxFrequencyText : "---" }
                                StatTile { Layout.fillWidth: true; caption: root.trText("逻辑处理器"); value: root.plugin ? String(root.plugin.logicalProcessorCount) : "---" }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredWidth: cards.columns === 2 ? (cards.width - 10) / 2 : cards.width
                        Layout.preferredHeight: 250
                        color: root.cPanel; radius: 9
                        border.width: 1; border.color: root.cBorder

                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 12; spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                SysInfoIcon { width: 18; height: 18; metric: "mem"; iconColor: root.cSuccess }
                                Text { text: root.trText("内存使用率"); color: root.cFg; font.pixelSize: 14; font.weight: Font.Bold }
                                Item { Layout.fillWidth: true }
                                Text { text: root.plugin ? root.plugin.memoryText : "---"; color: root.cSuccess; font.pixelSize: 18; font.weight: Font.Bold }
                            }
                            Text { text: root.trText("利用率"); color: root.cMuted; font.pixelSize: 10 }
                            SysInfoChart {
                                Layout.fillWidth: true; Layout.preferredHeight: 76
                                primarySeries: root.plugin ? root.plugin.memoryHistory : []
                                primaryColor: root.cSuccess; gridColor: root.cBorder; maximum: 100
                            }
                            GridLayout {
                                Layout.fillWidth: true; columns: 3; columnSpacing: 7; rowSpacing: 5
                                StatTile { Layout.fillWidth: true; caption: root.trText("已用内存"); value: root.plugin ? root.plugin.memoryUsedText : "---" }
                                StatTile { Layout.fillWidth: true; caption: root.trText("总内存"); value: root.plugin ? root.plugin.memoryTotalText : "---" }
                                StatTile { Layout.fillWidth: true; caption: root.trText("可用内存"); value: root.plugin ? root.plugin.memoryAvailableText : "---" }
                                StatTile { Layout.fillWidth: true; caption: root.trText("缓存"); value: root.plugin ? root.plugin.memoryCachedText : "---" }
                                StatTile { Layout.columnSpan: 2; Layout.fillWidth: true; caption: root.trText("已提交 / 限制"); value: root.plugin ? root.plugin.memoryCommitText : "---" }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredWidth: cards.columns === 2 ? (cards.width - 10) / 2 : cards.width
                        Layout.preferredHeight: 250
                        color: root.cPanel; radius: 9
                        border.width: 1; border.color: root.cBorder

                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 12; spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                SysInfoIcon { width: 18; height: 18; metric: "gpu"; iconColor: root.cWarning }
                                Text { text: root.trText("GPU 使用率"); color: root.cFg; font.pixelSize: 14; font.weight: Font.Bold }
                                Item { Layout.fillWidth: true }
                                Text { text: root.plugin ? root.plugin.gpuText : "---"; color: root.cWarning; font.pixelSize: 18; font.weight: Font.Bold }
                            }
                            Text { text: root.trText("利用率"); color: root.cMuted; font.pixelSize: 10 }
                            SysInfoChart {
                                Layout.fillWidth: true; Layout.preferredHeight: 92
                                primarySeries: root.plugin ? root.plugin.gpuHistory : []
                                primaryColor: root.cWarning; gridColor: root.cBorder; maximum: 100
                            }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 7
                                StatTile { Layout.fillWidth: true; caption: root.trText("显存占用 / 预算"); value: root.plugin ? root.plugin.gpuMemoryText : "---" }
                                StatTile { Layout.fillWidth: true; caption: root.trText("图形适配器"); value: root.plugin ? root.plugin.gpuName : "---" }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredWidth: cards.columns === 2 ? (cards.width - 10) / 2 : cards.width
                        Layout.preferredHeight: 250
                        color: root.cPanel; radius: 9
                        border.width: 1; border.color: root.cBorder

                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 12; spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                SysInfoIcon { width: 18; height: 18; metric: "netDown"; iconColor: root.cSuccess }
                                Text { text: root.trText("网络速率"); color: root.cFg; font.pixelSize: 14; font.weight: Font.Bold }
                                Item { Layout.fillWidth: true }
                                Rectangle { width: 7; height: 7; radius: 4; color: root.cSuccess }
                                Text { text: root.plugin ? root.plugin.netDownText : "---"; color: root.cMuted; font.pixelSize: 10 }
                                Rectangle { width: 7; height: 7; radius: 4; color: root.cDanger }
                                Text { text: root.plugin ? root.plugin.netUpText : "---"; color: root.cMuted; font.pixelSize: 10 }
                            }
                            Text { text: root.trText("下载 / 上传"); color: root.cMuted; font.pixelSize: 10 }
                            SysInfoChart {
                                Layout.fillWidth: true; Layout.preferredHeight: 92
                                primarySeries: root.plugin ? root.plugin.netDownHistory : []
                                secondarySeries: root.plugin ? root.plugin.netUpHistory : []
                                primaryColor: root.cSuccess; secondaryColor: root.cDanger
                                gridColor: root.cBorder; maximum: 0
                            }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 7
                                StatTile { Layout.fillWidth: true; caption: root.trText("累计接收"); value: root.plugin ? root.plugin.networkReceivedText : "---" }
                                StatTile { Layout.fillWidth: true; caption: root.trText("累计发送"); value: root.plugin ? root.plugin.networkSentText : "---" }
                                StatTile { Layout.fillWidth: true; caption: root.trText("活动接口"); value: root.plugin ? String(root.plugin.activeNetworkInterfaces) : "---" }
                            }
                        }
                    }
                }
            }
        }
    }
}
