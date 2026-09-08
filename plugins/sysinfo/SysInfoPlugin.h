// SysInfoPlugin.h — 系统信息插件（CPU/内存/GPU 占用率）
#pragma once
#include <QObject>
#include <QUrl>
#include <QPointer>
#include "barplugin/IBarPlugin2.h"

class SysInfoWorker;
class SysInfoViewModel;
class QThread;

class SysInfoPlugin : public QObject, public xuanhub::IBarPlugin2 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/2.0" FILE "metadata.json")
    Q_INTERFACES(xuanhub::IBarPlugin2)

public:
    xuanhub::PluginDescriptor descriptor() const override;
    xuanhub::PluginInitResult initialize(const xuanhub::PluginContext2& context) override;
    QObject* viewModel() override;
    QUrl    qmlUrl() const override;
    void    shutdown() override;
    xuanhub::PluginConfigValidationResult validateConfig(const QVariantMap& config) const override;
    xuanhub::PluginConfigApplyResult applyConfig(const QVariantMap& config) override;

private:
    QPointer<SysInfoViewModel> m_vm;
    QPointer<SysInfoWorker>    m_worker;
    QPointer<QThread>          m_thread;
    int m_intervalMs = 1000;
};
