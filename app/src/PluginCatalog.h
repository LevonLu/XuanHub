// PluginCatalog.h — 原生插件发现与预加载校验，不执行插件代码
#pragma once
#include "PluginManifest.h"
#include <QPluginLoader>
#include <QSet>
#include <memory>
#include <vector>

class PluginCatalog final {
public:
    struct Candidate {
        QString dirPath;
        QString path;
        QString iid;
        PluginManifestResult manifest;
        std::unique_ptr<QPluginLoader> loader;
    };
    struct Result {
        std::vector<Candidate> candidates;
        QHash<QString, QString> errors;
    };
    static Result scan(const QString& pluginsDir, const QString& configDir,
                       const QSet<QString>& enabledIds, const QSet<QString>& loadedIds,
                       const QHash<QString, QString>& unloadErrors);
};
