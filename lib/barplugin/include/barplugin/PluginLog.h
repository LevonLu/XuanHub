// PluginLog.h — 跨插件统一的带 ID 与稳定错误码结构化日志入口
#pragma once

#include <QString>

#include "barplugin_export.h"

namespace xuanhub {

class BARPLUGIN_EXPORT PluginLog final {
public:
    static QString format(const QString& pluginId, const QString& errorCode,
                          const QString& message);
    static void debug(const QString& pluginId, const QString& errorCode,
                      const QString& message);
    static void info(const QString& pluginId, const QString& errorCode,
                     const QString& message);
    static void warning(const QString& pluginId, const QString& errorCode,
                        const QString& message);
    static void critical(const QString& pluginId, const QString& errorCode,
                         const QString& message);
};

} // namespace xuanhub
