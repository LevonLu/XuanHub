// XuanHubMenuPlugin.cpp — 创建快速菜单 ViewModel 并接入宿主事件总线
#include "XuanHubMenuPlugin.h"
#include "XuanHubMenuViewModel.h"
#include "barplugin/BarPluginContext.h"

#include <QDebug>

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
    return true;
}

QObject* XuanHubMenuPlugin::viewModel() { return m_vm; }

QUrl XuanHubMenuPlugin::qmlUrl() const {
    return QUrl(QStringLiteral("XuanHubMenu.qml"));
}

void XuanHubMenuPlugin::shutdown() {
    // ViewModel 订阅了 EventBus；必须在 DLL unload 前删除，避免后续事件调用卸载代码。
    delete m_vm;
    m_vm = nullptr;
}
