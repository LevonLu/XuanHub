// BarPluginContext.h — 框架在 init 时注入给插件的能力包
#pragma once
#include <QString>
#include <QVariantMap>

namespace xuanhub {
class EventBus;

struct BarPluginContext {
    EventBus*   eventBus = nullptr;   // 事件总线（核心进程内单例）
    QString     configDir;            // %APPDATA%/XuanHub
    QVariantMap pluginConfig;         // config.json 里本插件那一段
};

} // namespace xuanhub
