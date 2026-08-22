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
    applyConfig(cfg);
    m_vm->start();
    return true;
}

QString ClockPlugin::validateConfig(const QVariantMap& config) const {
    const QVariant value = config.value(QStringLiteral("format"));
    if (value.isValid() && !value.canConvert<QString>())
        return QStringLiteral("format must be a string");
    return {};
}

bool ClockPlugin::applyConfig(const QVariantMap& config) {
    if (!m_vm || !validateConfig(config).isEmpty()) return false;
    QString format = config.value(QStringLiteral("format")).toString();
    if (format.isEmpty()) format = QStringLiteral("HH:mm:ss");
    m_vm->setFormat(format);
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
