// BarPluginContext.h — 框架在 init 时注入给插件的能力包
#pragma once
#include <QString>
#include <QHash>
#include <QVariantMap>

namespace xuanhub {
class EventBus;
class PluginEventScope;
class IHostServices;

struct BarPluginContext {
    EventBus*   eventBus = nullptr;   // 事件总线（核心进程内单例）
    QString     configDir;            // %APPDATA%/XuanHub
    QVariantMap pluginConfig;         // config.json 里本插件那一段
    PluginEventScope* eventScope = nullptr; // 宿主拥有；shutdown 前关闭并清除订阅
    QHash<QString, QString> dependencyVersions; // 已满足且已加载的依赖 ID → 版本
    IHostServices* hostServices = nullptr; // V1 兼容追加字段；旧二进制可安全忽略
};

} // namespace xuanhub
