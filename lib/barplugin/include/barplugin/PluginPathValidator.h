// PluginPathValidator.h — 跨原生与 Python 插件共享的目录边界校验
#pragma once

#include "barplugin_export.h"

#include <QString>

namespace xuanhub {

BARPLUGIN_EXPORT bool validatePluginFilePath(
    const QString& pluginDir,
    const QString& field,
    const QString& relativePath,
    QString* error = nullptr);

} // namespace xuanhub
