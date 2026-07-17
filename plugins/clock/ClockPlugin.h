// ClockPlugin.h — Clock 插件（示例：验证完整加载→渲染→刷新链路）
#pragma once
#include <QObject>
#include <QUrl>
#include "barplugin/IBarPlugin.h"

class ClockViewModel;

class ClockPlugin : public QObject, public xuanhub::IBarPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/1.0" FILE "metadata.json")
    Q_INTERFACES(xuanhub::IBarPlugin)

public:
    xuanhub::PluginMetadata metadata() const override;
    bool    init(xuanhub::BarPluginContext* ctx) override;
    QObject* viewModel() override;
    QUrl    qmlUrl() const override;
    void    shutdown() override;

private:
    ClockViewModel* m_vm = nullptr;
};
