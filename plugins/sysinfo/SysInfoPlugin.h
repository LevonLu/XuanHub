// SysInfoPlugin.h — 系统信息插件（CPU/内存/GPU 占用率）
#pragma once
#include <QObject>
#include <QUrl>
#include <QPointer>
#include "barplugin/IBarPlugin.h"
#include "barplugin/IBarPluginConfigurable.h"

class SysInfoWorker;
class SysInfoViewModel;
class QThread;

class SysInfoPlugin : public QObject, public xuanhub::IBarPlugin,
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
    QPointer<SysInfoViewModel> m_vm;
    QPointer<SysInfoWorker>    m_worker;
    QPointer<QThread>          m_thread;
    int m_intervalMs = 1000;
};
