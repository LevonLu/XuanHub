// XuanHubMenuViewModel.cpp — 发布快速菜单命令并接收宿主状态快照
#include "XuanHubMenuViewModel.h"
#include "barplugin/EventBus.h"
#include "barplugin/PluginEventScope.h"

#include <utility>

namespace {
QString actionKey(const QString& pluginId, const QString& actionId) {
    // Manifest 的 id 不允许控制字符；使用不可见分隔符避免点号、斜线等合法字符碰撞。
    return pluginId + QChar(0x1f) + actionId;
}
}

XuanHubMenuViewModel::XuanHubMenuViewModel(xuanhub::EventBus* bus,
                                           xuanhub::PluginEventScope* eventScope,
                                           QObject* parent)
    : QObject(parent), m_bus(bus) {
    if (eventScope) {
        eventScope->subscribe(QStringLiteral("app.quickMenu.state"), this,
                              "onState(QVariantMap)");
    } else if (m_bus) {
        m_bus->subscribe(QStringLiteral("app.quickMenu.state"), this,
                         "onState(QVariantMap)");
    }
}

void XuanHubMenuViewModel::requestState() {
    if (m_bus) m_bus->publish(QStringLiteral("app.quickMenu.requestState"));
}

void XuanHubMenuViewModel::invokeAction(const QString& action, bool enabled) {
    if (!m_bus || action.isEmpty()) return;
    QVariantMap payload;
    payload.insert(QStringLiteral("action"), action);
    payload.insert(QStringLiteral("enabled"), enabled);
    m_bus->publish(QStringLiteral("app.quickMenu.command"), payload);
}

bool XuanHubMenuViewModel::isPluginActionEnabled(const QString& pluginId,
                                                 const QString& actionId) const {
    return m_pluginActionsEnabled
        && !m_disabledPluginActions.contains(actionKey(pluginId, actionId));
}

void XuanHubMenuViewModel::applyConfig(const QVariantMap& config) {
    const bool actionsEnabled = config.value(
        QStringLiteral("pluginActionsEnabled"), true).toBool();
    QSet<QString> disabled;
    const QVariantMap byPlugin = config.value(
        QStringLiteral("disabledPluginActions")).toMap();
    for (auto it = byPlugin.cbegin(); it != byPlugin.cend(); ++it) {
        const QString pluginId = it.key();
        for (const QVariant& value : it.value().toList()) {
            const QString actionId = value.toString();
            if (!pluginId.isEmpty() && !actionId.isEmpty())
                disabled.insert(actionKey(pluginId, actionId));
        }
    }

    if (actionsEnabled == m_pluginActionsEnabled
        && disabled == m_disabledPluginActions) {
        return;
    }
    m_pluginActionsEnabled = actionsEnabled;
    m_disabledPluginActions = std::move(disabled);
    ++m_actionFilterRevision;
    emit actionFilterChanged();
}

void XuanHubMenuViewModel::onState(QVariantMap payload) {
    const bool autoStart = payload.value(QStringLiteral("autoStartEnabled")).toBool();
    const bool taskbar = payload.value(QStringLiteral("taskbarMode")).toBool();
    const QString theme = payload.value(QStringLiteral("themeMode"),
                                        QStringLiteral("dark")).toString();
    if (autoStart == m_autoStartEnabled && taskbar == m_taskbarMode
        && theme == m_themeMode) {
        return;
    }
    m_autoStartEnabled = autoStart;
    m_taskbarMode = taskbar;
    m_themeMode = theme;
    emit stateChanged();
}
