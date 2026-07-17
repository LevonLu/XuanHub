// LauncherPlugin.h — 快捷启动器插件
#pragma once
#include <QObject>
#include <QUrl>
#include <QPointer>
#include "barplugin/IBarPlugin.h"

class LauncherViewModel;

class LauncherPlugin : public QObject, public xuanhub::IBarPlugin {
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
    QPointer<LauncherViewModel> m_vm;
};
