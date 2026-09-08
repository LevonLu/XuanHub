// PluginReconciler.h — 将期望配置增量应用到插件运行时
#pragma once
#include "ConfigService.h"
class PluginManager;
class PluginReconciler final {
public:
    // 返回 true 表示已有配置在等待确认，最新配置需在回执后再应用。
    static bool apply(PluginManager& manager, const QString& pluginsDir,
                      const QList<PluginConfigEntry>& desired);
};
