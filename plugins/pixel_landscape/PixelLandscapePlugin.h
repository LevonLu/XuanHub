// PixelLandscapePlugin.h — 像素小风景 插件入口
#pragma once

#include <QObject>
#include <QUrl>
#include "barplugin/IBarPlugin2.h"

class PixelLandscapeViewModel;

class PixelLandscapePlugin final : public QObject, public xuanhub::IBarPlugin2 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/2.0" FILE "metadata.json")
    Q_INTERFACES(xuanhub::IBarPlugin2)

public:
    xuanhub::PluginDescriptor descriptor() const override;
    xuanhub::PluginInitResult initialize(const xuanhub::PluginContext2& context) override;
    xuanhub::PluginConfigValidationResult validateConfig(const QVariantMap& config) const override;
    xuanhub::PluginConfigApplyResult applyConfig(const QVariantMap& config) override;
    QObject* viewModel() override;
    QUrl qmlUrl() const override;
    void shutdown() override;

private:
    PixelLandscapeViewModel* m_viewModel = nullptr;
};
