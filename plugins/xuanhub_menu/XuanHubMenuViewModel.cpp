// XuanHubMenuViewModel.cpp — 发布快速菜单命令并接收宿主状态快照
#include "XuanHubMenuViewModel.h"
#include "barplugin/EventBus.h"
#include "barplugin/PluginEventScope.h"

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
