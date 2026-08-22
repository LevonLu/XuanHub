// PluginRuntime.h — 把 V1/V2 插件适配为宿主内部统一生命周期
#pragma once

#include "barplugin/PluginDescriptor.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <memory>

namespace xuanhub {
class IBarPlugin;
class IBarPlugin2;
struct BarPluginContext;
}

class PluginRuntime final {
public:
    enum class ApplyStatus { Unsupported, Applied, RequiresRestart, Rejected };
    enum class MigrationStatus { NotRequired, Unsupported, Migrated, Rejected };
    struct ApplyResult {
        ApplyStatus status = ApplyStatus::Unsupported;
        QString error;
    };
    struct MigrationResult {
        MigrationStatus status = MigrationStatus::Unsupported;
        int targetVersion = 1;
        QVariantMap config;
        QString error;
    };

    static std::unique_ptr<PluginRuntime> create(
        QObject* instance, const QString& iid, QString* error = nullptr);

    QObject* instance() const { return m_instance; }
    bool isV2() const { return m_v2 != nullptr; }
    xuanhub::PluginDescriptor descriptor() const;
    bool descriptorMatches(const xuanhub::PluginDescriptor& manifest,
                           QString* error = nullptr) const;
    bool initialize(xuanhub::BarPluginContext* context, QString* error = nullptr);
    QObject* viewModel() const;
    QUrl qmlUrl() const;
    ApplyResult applyConfig(const QVariantMap& config);
    MigrationResult migrateConfig(int currentVersion,
                                  const QVariantMap& config) const;
    void shutdown();

private:
    PluginRuntime(QObject* instance, xuanhub::IBarPlugin* v1,
                  xuanhub::IBarPlugin2* v2);

    QObject* m_instance = nullptr;
    xuanhub::IBarPlugin* m_v1 = nullptr;
    xuanhub::IBarPlugin2* m_v2 = nullptr;
};
