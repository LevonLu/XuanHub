// PythonPluginManifest.h — 在执行 Python 插件代码前解析并校验 sidecar Manifest
#pragma once

#include "PluginManifest.h"

class PythonPluginManifest final {
public:
    static PluginManifestResult load(
        const QString& pluginDir,
        const PluginValidationEnvironment& environment =
            PluginValidationEnvironment::current());
};
