// PluginContractValidator.h — 统一校验插件 Manifest、设置 Schema 与语言包契约
#pragma once

#include "PluginManifest.h"

#include <QStringList>

struct PluginContractValidationResult {
    PluginManifestResult manifest;
    QStringList errors;
    QStringList warnings;

    bool ok() const { return manifest.ok && errors.isEmpty(); }
};

class PluginContractValidator final {
public:
    static PluginContractValidationResult validate(
        const QString& pluginDir, const QString& runtime,
        const QString& discoveredBackend = QString(),
        const PluginValidationEnvironment& environment =
            PluginValidationEnvironment::current());
};
