// PluginMetadata.h — 插件元数据（POD）
#pragma once
#include <QString>

namespace xuanhub {

struct PluginMetadata {
    QString id;          // 全局唯一，配置/EventBus 寻址
    QString displayName; // 显示名
    QString version;
    QString author;
};

} // namespace xuanhub
