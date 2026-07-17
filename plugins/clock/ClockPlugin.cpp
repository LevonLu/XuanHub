// ClockPlugin.cpp
#include "ClockPlugin.h"
#include "ClockViewModel.h"

using namespace xuanhub;

static const PluginMetadata kMeta = {
    QStringLiteral("clock"),
    QStringLiteral("Clock"),
    QStringLiteral("0.1.0"),
    QStringLiteral("XuanHub"),
};

PluginMetadata ClockPlugin::metadata() const { return kMeta; }

bool ClockPlugin::init(BarPluginContext* ctx) {
    const QVariantMap cfg = ctx ? ctx->pluginConfig : QVariantMap();
    m_vm = new ClockViewModel();
    const QString fmt = cfg.value(QStringLiteral("format")).toString();
    if (!fmt.isEmpty()) m_vm->setFormat(fmt);
    m_vm->start();
    return true;
}

QObject* ClockPlugin::viewModel() { return m_vm; }

QUrl ClockPlugin::qmlUrl() const { return QUrl(QStringLiteral("Clock.qml")); }

void ClockPlugin::shutdown() {
    if (m_vm) {
        m_vm->stop();
        delete m_vm;
        m_vm = nullptr;
    }
}
