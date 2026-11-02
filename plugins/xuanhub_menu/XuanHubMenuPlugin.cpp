// XuanHubMenuPlugin.cpp — 创建快速菜单 ViewModel 并接入宿主事件总线
#include "XuanHubMenuPlugin.h"
#include "XuanHubMenuViewModel.h"
#include "barplugin/PluginEventScope.h"

#include <QMetaType>

using namespace xuanhub;

xuanhub::PluginDescriptor XuanHubMenuPlugin::descriptor() const {
    xuanhub::PluginDescriptor value;
    value.id = QStringLiteral("xuanhub_menu");
    value.displayName = QStringLiteral("玄枢菜单");
    value.version = QStringLiteral("0.2.1");
    value.apiVersion = 2;
    value.configVersion = 1;
    value.minHostVersion = QStringLiteral("0.4.1");
    value.maxHostVersion = QStringLiteral("0.x");
    value.author = QStringLiteral("XuanHub");
    value.entry.backend = QStringLiteral("xuanhub_menu.dll");
    value.entry.qml = QStringLiteral("XuanHubMenu.qml");
    value.entry.settingsQml = QStringLiteral("XuanHubMenuSettings.qml");
    value.capabilities = {QStringLiteral("barItem"), QStringLiteral("settings"),
                          QStringLiteral("popup")};
    value.permissions = {QStringLiteral("events.publish:app.quickMenu.requestState"),
                         QStringLiteral("events.publish:app.quickMenu.command")};
    value.compatibility.architecture = QStringLiteral("x64");
    value.compatibility.qtVersion = QStringLiteral("6.9.3");
    value.compatibility.compiler = QStringLiteral("msvc2022");
    value.compatibility.crt = QStringLiteral("dynamic");
    return value;
}

PluginInitResult XuanHubMenuPlugin::initialize(const PluginContext2& context) {
    if (m_vm)
        return PluginInitResult::failed(QStringLiteral("plugin is already initialized"));
    if (!context.eventScope || !context.eventScope->active())
        return PluginInitResult::failed(QStringLiteral("EventBus V2 scope unavailable"));
    const auto validation = validateConfig(context.pluginConfig);
    if (!validation.valid) return PluginInitResult::failed(validation.error);
    m_vm = new XuanHubMenuViewModel(context.eventScope);
    m_vm->applyConfig(context.pluginConfig);
    return PluginInitResult::succeeded();
}

QObject* XuanHubMenuPlugin::viewModel() { return m_vm; }

QUrl XuanHubMenuPlugin::qmlUrl() const {
    return QUrl(QStringLiteral("XuanHubMenu.qml"));
}

PluginConfigValidationResult XuanHubMenuPlugin::validateConfig(const QVariantMap& config) const {
    const auto enabled = config.constFind(QStringLiteral("pluginActionsEnabled"));
    if (enabled != config.cend() && enabled->metaType().id() != QMetaType::Bool)
        return PluginConfigValidationResult::rejected(
            QStringLiteral("pluginActionsEnabled must be a boolean"));

    const auto disabled = config.constFind(QStringLiteral("disabledPluginActions"));
    if (disabled == config.cend()) return PluginConfigValidationResult::accepted();
    if (disabled->metaType().id() != QMetaType::QVariantMap)
        return PluginConfigValidationResult::rejected(
            QStringLiteral("disabledPluginActions must be an object"));
    const QVariantMap byPlugin = disabled->toMap();
    for (auto it = byPlugin.cbegin(); it != byPlugin.cend(); ++it) {
        if (it.key().isEmpty() || it.value().metaType().id() != QMetaType::QVariantList)
            return PluginConfigValidationResult::rejected(
                QStringLiteral("disabledPluginActions entries must be arrays"));
        for (const QVariant& value : it.value().toList()) {
            if (value.metaType().id() != QMetaType::QString
                || value.toString().isEmpty()) {
                return PluginConfigValidationResult::rejected(
                    QStringLiteral("disabledPluginActions action ids must be strings"));
            }
        }
    }
    return PluginConfigValidationResult::accepted();
}

PluginConfigApplyResult XuanHubMenuPlugin::applyConfig(const QVariantMap& config) {
    if (!m_vm)
        return PluginConfigApplyResult::rejected(QStringLiteral("plugin is not initialized"));
    const auto validation = validateConfig(config);
    if (!validation.valid) return PluginConfigApplyResult::rejected(validation.error);
    m_vm->applyConfig(config);
    return PluginConfigApplyResult::applied();
}

void XuanHubMenuPlugin::shutdown() {
    // ViewModel 订阅了 EventBus；必须在 DLL unload 前删除，避免后续事件调用卸载代码。
    delete m_vm;
    m_vm = nullptr;
}
