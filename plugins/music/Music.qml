// Music.qml — 状态栏歌词与播放控制弹窗
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub

Item {
    id: root
    property var plugin
    property var pluginContext
    property real hostContentWidth: -1
    property real hostContentHeight: -1
    readonly property var colors: pluginContext ? pluginContext.colors : ({
        fg: "#ffffff", muted: "#b0b0b0", panel: "#202020",
        border: "#484848", hover: "#383838", accent: "#60cdff"
    })
    readonly property string track: plugin && plugin.title ? plugin.title : ""
    readonly property string current: plugin && plugin.currentLine ? plugin.currentLine : track
    readonly property bool doubleLine: !!(plugin && plugin.twoLines && height >= 32)
    readonly property bool uniformLineFont: !!(plugin && plugin.uniformLineFont)

    implicitWidth: 240
    width: hostContentWidth >= 0 ? hostContentWidth : implicitWidth
    height: hostContentHeight >= 0 ? hostContentHeight : 40

    component LyricText: Text {
        color: root.colors.fg
        elide: Text.ElideRight
        textFormat: Text.PlainText
        font.pixelSize: 13
        verticalAlignment: Text.AlignVCenter
    }

    component PlayerButton: Button {
        palette.button: root.colors.hover
        palette.mid: root.colors.border
        palette.buttonText: root.colors.fg
        palette.highlight: root.colors.accent
        opacity: enabled ? 1 : 0.48
    }

    Column {
        readonly property real inset: Math.min(8, root.width / 4)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: root.width - inset * 2
        LyricText {
            objectName: "musicCurrentLine"
            width: parent.width
            height: root.doubleLine ? (root.uniformLineFont ? 16 : 18) : root.height
            font.pixelSize: root.doubleLine ? (root.uniformLineFont ? 14 : 15) : 13
            text: root.current
        }
        LyricText {
            objectName: "musicNextLine"
            width: parent.width
            height: root.uniformLineFont ? 16 : 14
            visible: root.doubleLine
            text: root.plugin && root.plugin.nextLine ? root.plugin.nextLine : ""
            color: root.colors.muted
            font.pixelSize: root.uniformLineFont ? 14 : 11
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.track.length > 0
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button
        Accessible.name: qsTranslate("plugin.music.main", "打开音乐控制")
        onClicked: popup.toggleFromBar()
    }

    PluginPopup {
        id: popup
        objectName: "musicPopup"
        anchorItem: root
        pluginContext: root.pluginContext
        readonly property var available: root.pluginContext ? root.pluginContext.screenGeometry : ({})
        width: Math.max(1, Math.min(376, Number(available.width || 800) - 16))
        height: 238

        Loader {
            anchors.fill: parent
            active: popup.visible
            sourceComponent: panelBody
        }
    }

    Component {
        id: panelBody
        Rectangle {
            color: root.colors.panel
            border.color: root.colors.border
            radius: 10

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 4

                LyricText {
                    Layout.fillWidth: true
                    text: root.track
                    font.pixelSize: 17
                    font.bold: true
                }
                LyricText {
                    Layout.fillWidth: true
                    text: root.plugin && root.plugin.artist ? root.plugin.artist : ""
                    color: root.colors.muted
                    font.pixelSize: 12
                }
                Item { Layout.fillHeight: true }
                LyricText {
                    Layout.fillWidth: true
                    text: root.plugin && root.plugin.previousLine ? root.plugin.previousLine : ""
                    color: root.colors.muted
                }
                LyricText {
                    Layout.fillWidth: true
                    objectName: "musicPopupCurrentLine"
                    text: root.plugin && root.plugin.currentLine ? root.plugin.currentLine : ""
                    font.bold: true
                    color: root.colors.accent
                }
                LyricText {
                    Layout.fillWidth: true
                    text: root.plugin && root.plugin.nextLine ? root.plugin.nextLine : ""
                    color: root.colors.muted
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true
                    LyricText {
                        text: progress.formatTime(progress.value)
                        font.pixelSize: 11
                        color: root.colors.muted
                    }
                    ProgressBar {
                        id: progress
                        Layout.fillWidth: true
                        from: 0
                        to: root.plugin && root.plugin.durationMs ? root.plugin.durationMs : 1
                        palette.accent: root.colors.accent
                        palette.dark: root.colors.accent
                        palette.midlight: root.colors.hover

                        function refresh() {
                            value = root.plugin && typeof root.plugin.positionMs === "function"
                                ? Math.min(to, root.plugin.positionMs()) : 0
                        }
                        function formatTime(ms) {
                            const seconds = Math.floor(ms / 1000)
                            return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
                        }
                        Component.onCompleted: refresh()
                        Timer {
                            interval: 250
                            running: true
                            repeat: true
                            onTriggered: progress.refresh()
                        }
                    }
                    LyricText {
                        text: progress.formatTime(progress.to)
                        font.pixelSize: 11
                        color: root.colors.muted
                    }
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    PlayerButton {
                        objectName: "musicPrevious"
                        text: qsTranslate("plugin.music.main", "上一曲")
                        enabled: !!(root.plugin && root.plugin.canPrevious)
                        onClicked: root.plugin.previous()
                    }
                    PlayerButton {
                        objectName: "musicToggle"
                        text: root.plugin && root.plugin.playing
                            ? qsTranslate("plugin.music.main", "暂停")
                            : qsTranslate("plugin.music.main", "播放")
                        enabled: !!(root.plugin && root.plugin.canToggle)
                        onClicked: root.plugin.togglePlayback()
                    }
                    PlayerButton {
                        objectName: "musicNext"
                        text: qsTranslate("plugin.music.main", "下一曲")
                        enabled: !!(root.plugin && root.plugin.canNext)
                        onClicked: root.plugin.next()
                    }
                }
                LyricText {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 14
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 11
                    color: root.colors.muted
                    text: root.plugin && root.plugin.commandFailed
                        ? qsTranslate("plugin.music.main", "播放器未执行操作") : ""
                }
            }
        }
    }
}

