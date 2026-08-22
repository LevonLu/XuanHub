// IPluginConfigMigration.h — 可选的插件配置迁移接口
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace xuanhub {

struct PluginConfigMigrationResult {
    bool success = false;
    QVariantMap config;
    QString error;

    static PluginConfigMigrationResult migrated(const QVariantMap& config) {
        return {true, config, {}};
    }
    static PluginConfigMigrationResult failed(const QString& error) {
        return {false, {}, error};
    }
};

// 插件对象按需实现此接口。宿主只传临时副本，插件不得自行写 config.json 或密钥库；
// 返回值代表从 fromVersion 一次迁移到 Descriptor.configVersion 的完整结果。
class IPluginConfigMigration {
public:
    virtual ~IPluginConfigMigration() = default;
    virtual PluginConfigMigrationResult migrateConfig(
        int fromVersion, const QVariantMap& config) const = 0;
};

} // namespace xuanhub

Q_DECLARE_INTERFACE(xuanhub::IPluginConfigMigration,
                    "xuanhub.PluginConfigMigration/1.0")
