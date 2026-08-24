// XuanHubMenuPlugin.h — bar 上的玄枢快速菜单插件入口
#pragma once
#include <QObject>
#include <QUrl>
#include "barplugin/IBarPlugin.h"
#include "barplugin/IBarPluginConfigurable.h"

class XuanHubMenuViewModel;

class XuanHubMenuPlugin : public QObject,
                          public xuanhub::IBarPlugin,
                          public xuanhub::IBarPluginConfigurable {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/1.0" FILE "metadata.json")
    Q_INTERFACES(xuanhub::IBarPlugin xuanhub::IBarPluginConfigurable)

public:
    xuanhub::PluginMetadata metadata() const override;
    bool init(xuanhub::BarPluginContext* ctx) override;
    QObject* viewModel() override;
    QUrl qmlUrl() const override;
    void shutdown() override;
    QString validateConfig(const QVariantMap& config) const override;
    bool applyConfig(const QVariantMap& config) override;

private:
    XuanHubMenuViewModel* m_vm = nullptr;
};
