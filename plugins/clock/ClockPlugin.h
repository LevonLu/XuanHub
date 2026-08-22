// ClockPlugin.h — Clock 插件（示例：验证完整加载→渲染→刷新链路）
#pragma once
#include <QObject>
#include <QUrl>
#include "barplugin/IBarPlugin2.h"
#include "barplugin/IPluginConfigMigration.h"

class ClockViewModel;

class ClockPlugin : public QObject, public xuanhub::IBarPlugin2,
                    public xuanhub::IPluginConfigMigration {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/2.0" FILE "metadata.json")
    Q_INTERFACES(xuanhub::IBarPlugin2 xuanhub::IPluginConfigMigration)

public:
    xuanhub::PluginDescriptor descriptor() const override;
    xuanhub::PluginInitResult initialize(const xuanhub::PluginContext2& context) override;
    QObject* viewModel() override;
    QUrl    qmlUrl() const override;
    void    shutdown() override;
    xuanhub::PluginConfigValidationResult validateConfig(
        const QVariantMap& config) const override;
    xuanhub::PluginConfigApplyResult applyConfig(
        const QVariantMap& config) override;
    xuanhub::PluginConfigMigrationResult migrateConfig(
        int fromVersion, const QVariantMap& config) const override;

private:
    ClockViewModel* m_vm = nullptr;
};
