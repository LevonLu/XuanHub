// PluginManifest.h — 加载插件代码前解析并校验 sidecar Manifest
#pragma once

#include "barplugin/PluginDescriptor.h"

#include <QString>

struct PluginValidationEnvironment {
    QString hostVersion;
    QString architecture;
    QString qtVersion;
    QString compiler;
    QString crt;

    static PluginValidationEnvironment current();
};

struct PluginManifestResult {
    bool ok = false;
    xuanhub::PluginDescriptor descriptor;
    QString error;
};

class PluginManifest final {
public:
    static PluginManifestResult load(
        const QString& pluginDir,
        const QString& discoveredBackend,
        const PluginValidationEnvironment& environment);
};
