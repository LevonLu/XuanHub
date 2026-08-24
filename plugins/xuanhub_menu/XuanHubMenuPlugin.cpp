// XuanHubMenuPlugin.cpp — 创建快速菜单 ViewModel 并接入宿主事件总线
#include "XuanHubMenuPlugin.h"
#include "XuanHubMenuViewModel.h"
#include "barplugin/BarPluginContext.h"

#include <QDebug>
#include <QMetaType>

using namespace xuanhub;

static const PluginMetadata kMeta = {
    QStringLiteral("xuanhub_menu"),
    QStringLiteral("XuanHub Menu"),
    QStringLiteral("0.1.0"),
    QStringLiteral("XuanHub"),
};

PluginMetadata XuanHubMenuPlugin::metadata() const { return kMeta; }

bool XuanHubMenuPlugin::init(BarPluginContext* ctx) {
    if (!ctx || !ctx->eventBus) {
        qWarning() << "[XuanHubMenu] EventBus unavailable";
        return false;
    }
    m_vm = new XuanHubMenuViewModel(ctx->eventBus, ctx->eventScope);
    m_vm->applyConfig(ctx->pluginConfig);
    return true;
}

QObject* XuanHubMenuPlugin::viewModel() { return m_vm; }

QUrl XuanHubMenuPlugin::qmlUrl() const {
    return QUrl(QStringLiteral("XuanHubMenu.qml"));
}

QString XuanHubMenuPlugin::validateConfig(const QVariantMap& config) const {
    const auto enabled = config.constFind(QStringLiteral("pluginActionsEnabled"));
    if (enabled != config.cend() && enabled->metaType().id() != QMetaType::Bool)
        return QStringLiteral("pluginActionsEnabled must be a boolean");

    const auto disabled = config.constFind(QStringLiteral("disabledPluginActions"));
    if (disabled == config.cend()) return {};
    if (disabled->metaType().id() != QMetaType::QVariantMap)
        return QStringLiteral("disabledPluginActions must be an object");
    const QVariantMap byPlugin = disabled->toMap();
    for (auto it = byPlugin.cbegin(); it != byPlugin.cend(); ++it) {
        if (it.key().isEmpty() || it.value().metaType().id() != QMetaType::QVariantList)
            return QStringLiteral("disabledPluginActions entries must be arrays");
        for (const QVariant& value : it.value().toList()) {
            if (value.metaType().id() != QMetaType::QString
                || value.toString().isEmpty()) {
                return QStringLiteral("disabledPluginActions action ids must be strings");
            }
        }
    }
    return {};
}

bool XuanHubMenuPlugin::applyConfig(const QVariantMap& config) {
    if (!m_vm || !validateConfig(config).isEmpty()) return false;
    m_vm->applyConfig(config);
    return true;
}

void XuanHubMenuPlugin::shutdown() {
    // ViewModel 订阅了 EventBus；必须在 DLL unload 前删除，避免后续事件调用卸载代码。
    delete m_vm;
    m_vm = nullptr;
}
