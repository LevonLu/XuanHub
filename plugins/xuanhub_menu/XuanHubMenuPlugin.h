// XuanHubMenuPlugin.h — bar 上的玄枢快速菜单插件入口
#pragma once
#include <QObject>
#include <QUrl>
#include "barplugin/IBarPlugin2.h"

class XuanHubMenuViewModel;

class XuanHubMenuPlugin : public QObject,
                          public xuanhub::IBarPlugin2 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/2.0" FILE "metadata.json")
    Q_INTERFACES(xuanhub::IBarPlugin2)

public:
    xuanhub::PluginDescriptor descriptor() const override;
    xuanhub::PluginInitResult initialize(const xuanhub::PluginContext2& context) override;
    QObject* viewModel() override;
    QUrl qmlUrl() const override;
    void shutdown() override;
    xuanhub::PluginConfigValidationResult validateConfig(const QVariantMap& config) const override;
    xuanhub::PluginConfigApplyResult applyConfig(const QVariantMap& config) override;

private:
    XuanHubMenuViewModel* m_vm = nullptr;
};
