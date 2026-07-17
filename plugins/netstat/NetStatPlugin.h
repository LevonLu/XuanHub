// NetStatPlugin.h — 网络流量插件
#pragma once
#include <QObject>
#include <QUrl>
#include <QPointer>
#include "barplugin/IBarPlugin.h"

class NetStatWorker;
class NetStatViewModel;
class QThread;

class NetStatPlugin : public QObject, public xuanhub::IBarPlugin {
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
    QPointer<NetStatViewModel> m_vm;
    QPointer<NetStatWorker>    m_worker;
    QPointer<QThread>          m_thread;
    int m_intervalMs = 1000;
};
