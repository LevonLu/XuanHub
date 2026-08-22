// ClockPlugin.h — Clock 插件（示例：验证完整加载→渲染→刷新链路）
#pragma once
#include <QObject>
#include <QUrl>
#include "barplugin/IBarPlugin.h"
#include "barplugin/IBarPluginConfigurable.h"

class ClockViewModel;

class ClockPlugin : public QObject, public xuanhub::IBarPlugin,
                    public xuanhub::IBarPluginConfigurable {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/1.0" FILE "metadata.json")
    Q_INTERFACES(xuanhub::IBarPlugin xuanhub::IBarPluginConfigurable)

public:
    xuanhub::PluginMetadata metadata() const override;
    bool    init(xuanhub::BarPluginContext* ctx) override;
    QObject* viewModel() override;
    QUrl    qmlUrl() const override;
    void    shutdown() override;
    QString validateConfig(const QVariantMap& config) const override;
    bool applyConfig(const QVariantMap& config) override;

private:
    ClockViewModel* m_vm = nullptr;
};
