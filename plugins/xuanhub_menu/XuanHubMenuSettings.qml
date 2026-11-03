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
        spacing: UiMetrics.space2

        UiCard {
            Layout.fillWidth: true
            compact: true

            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.space3
                Label {
                    Layout.fillWidth: true
                    text: root.trText("显示其他插件的快捷操作")
                    font.pixelSize: UiMetrics.fontM
                    wrapMode: Text.Wrap
                }
                UiSwitch {
                    objectName: "menuPluginActionsToggle"
                    Accessible.name: root.trText("显示其他插件的快捷操作")
                    checked: root.pluginActionsEnabled
                    onToggled: {
                        root.pluginActionsEnabled = checked
                        if (root.plugin)
                            root.plugin.setValue("pluginActionsEnabled", checked)
                    }
                }
            }

            // 总开关关闭时收起明细，但保留每个动作的草稿选择。
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.pluginActionsEnabled
                spacing: UiMetrics.space1
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: palette.mid
                    opacity: 0.15
                }
                Repeater {
                    model: root.availableActions
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: UiMetrics.space3
                        Label {
                            Layout.fillWidth: true
                            text: modelData.label
                            font.pixelSize: UiMetrics.fontM
                            elide: Text.ElideRight
                            HoverHandler { id: actionHover }
                            ToolTip.visible: actionHover.hovered
                            ToolTip.text: modelData.pluginId + " · " + modelData.actionId
                        }
                        UiSwitch {
                            objectName: "menuAction_" + modelData.pluginId + "_" + modelData.actionId
                            Accessible.name: modelData.label
                            checked: root.actionEnabled(modelData.pluginId, modelData.actionId)
                            onToggled: root.setActionEnabled(modelData.pluginId, modelData.actionId, checked)
                        }
                    }
                }
                Label {
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
}
