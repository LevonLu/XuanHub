// XuanHubMenuSettings.qml — 控制其他插件向玄枢菜单贡献的快捷动作
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub

Item {
    id: root
    property var plugin
    property bool pluginActionsEnabled: true
    property var disabledPluginActions: ({})
    property var availableActions: []
    property bool _loaded: false

    readonly property int registryRevision:
        (typeof pluginActions !== "undefined" && pluginActions)
        ? pluginActions.revision : 0

    implicitHeight: content.implicitHeight

    function trText(source) {
        return qsTranslate("plugin.xuanhub_menu.settingsPage", source)
    }

    function reloadActions() {
        if (typeof pluginActions === "undefined" || !pluginActions) {
            availableActions = []
            return
        }
        const revision = pluginActions.revision
        availableActions = pluginActions.actions("quickMenu").filter(function(action) {
            return action.pluginId !== "xuanhub_menu"
        })
    }

    function loadDraft() {
        if (!plugin || _loaded) return
        _loaded = true
        const enabledValue = plugin.value("pluginActionsEnabled")
        pluginActionsEnabled = enabledValue === undefined || enabledValue === null
                             ? true : enabledValue === true
        const disabledValue = plugin.value("disabledPluginActions")
        disabledPluginActions = disabledValue && typeof disabledValue === "object"
                              ? disabledValue : ({})
        reloadActions()
    }

    function actionEnabled(pluginId, actionId) {
        const ids = disabledPluginActions && disabledPluginActions[pluginId]
                  ? disabledPluginActions[pluginId] : []
        return ids.indexOf(actionId) < 0
    }

    function setActionEnabled(pluginId, actionId, enabled) {
        if (!plugin) return
        // 深拷贝后整体写回，确保 QVariantMap/QVariantList 的草稿变更能被宿主观察。
        const next = JSON.parse(JSON.stringify(disabledPluginActions || {}))
        const ids = Array.isArray(next[pluginId]) ? next[pluginId].slice(0) : []
        const index = ids.indexOf(actionId)
        if (enabled && index >= 0) ids.splice(index, 1)
        if (!enabled && index < 0) ids.push(actionId)
        if (ids.length > 0) next[pluginId] = ids
        else delete next[pluginId]
        disabledPluginActions = next
        plugin.setValue("disabledPluginActions", next)
    }

    onPluginChanged: loadDraft()
    onRegistryRevisionChanged: reloadActions()

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: UiMetrics.space4

        UiCard {
            Layout.fillWidth: true
            title: root.trText("插件快捷操作")
            description: root.trText("控制其他插件是否可以在玄枢菜单中加入快捷按钮")

            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.space3

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: UiMetrics.space1
                    Text {
                        text: root.trText("显示其他插件的快捷操作")
                        color: palette.windowText
                        font.pixelSize: UiMetrics.fontM
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.trText("关闭后，玄枢菜单只保留自身的设置与系统操作")
                        color: palette.placeholderText
                        font.pixelSize: UiMetrics.fontTiny
                        wrapMode: Text.Wrap
                    }
                }

                UiSwitch {
                    checked: root.pluginActionsEnabled
                    Layout.preferredWidth: 42
                    onToggled: {
                        root.pluginActionsEnabled = checked
                        if (root.plugin)
                            root.plugin.setValue("pluginActionsEnabled", checked)
                    }
                }
            }
        }

        UiCard {
            Layout.fillWidth: true
            title: root.trText("单独控制")
            description: root.trText("已发现的插件动作会列在这里；新动作默认启用")

            Repeater {
                model: root.availableActions

                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 54
                    radius: UiMetrics.radiusS
                    color: Qt.rgba(palette.base.r, palette.base.g, palette.base.b, 0.72)
                    border.width: 1
                    border.color: Qt.rgba(palette.mid.r, palette.mid.g, palette.mid.b, 0.36)
                    opacity: root.pluginActionsEnabled ? 1.0 : 0.55

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: UiMetrics.space3
                        anchors.rightMargin: UiMetrics.space3
                        spacing: UiMetrics.space3

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                text: modelData.label
                                color: palette.windowText
                                font.pixelSize: UiMetrics.fontM
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.pluginId + " · " + modelData.actionId
                                color: palette.placeholderText
                                font.pixelSize: UiMetrics.fontTiny
                                elide: Text.ElideRight
                            }
                        }

                        UiSwitch {
                            enabled: root.pluginActionsEnabled
                            checked: root.actionEnabled(modelData.pluginId,
                                                        modelData.actionId)
                            Layout.preferredWidth: 42
                            onToggled: root.setActionEnabled(modelData.pluginId,
                                                             modelData.actionId,
                                                             checked)
                        }
                    }
                }
            }

            Text {
                visible: root.availableActions.length === 0
                Layout.fillWidth: true
                text: root.trText("当前没有其他插件提供快捷操作")
                color: palette.placeholderText
                font.pixelSize: UiMetrics.fontS
                wrapMode: Text.Wrap
            }
        }
    }
}
