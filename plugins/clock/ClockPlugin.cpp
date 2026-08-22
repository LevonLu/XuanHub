// ClockPlugin.cpp
#include "ClockPlugin.h"
#include "ClockViewModel.h"

using namespace xuanhub;

PluginDescriptor ClockPlugin::descriptor() const {
    PluginDescriptor value;
    value.id = QStringLiteral("clock");
    value.displayName = QStringLiteral("Clock");
    value.version = QStringLiteral("0.1.0");
    value.apiVersion = 2;
    value.minHostVersion = QStringLiteral("0.2.1");
    value.maxHostVersion = QStringLiteral("0.x");
    value.author = QStringLiteral("XuanHub");
    value.entry.backend = QStringLiteral("clock.dll");
    value.entry.qml = QStringLiteral("Clock.qml");
    value.capabilities = {QStringLiteral("barItem"), QStringLiteral("settings"),
                          QStringLiteral("popup")};
    value.compatibility.architecture = QStringLiteral("x64");
    value.compatibility.qtVersion = QStringLiteral("6.9.3");
    value.compatibility.compiler = QStringLiteral("msvc2022");
    value.compatibility.crt = QStringLiteral("dynamic");
    return value;
}

PluginInitResult ClockPlugin::initialize(const PluginContext2& context) {
    m_vm = new ClockViewModel();
    const auto applied = applyConfig(context.pluginConfig);
    if (applied.disposition == PluginConfigApplyDisposition::Rejected) {
        delete m_vm;
        m_vm = nullptr;
        return PluginInitResult::failed(applied.error);
    }
    m_vm->start();
    return PluginInitResult::succeeded();
}

xuanhub::PluginConfigValidationResult ClockPlugin::validateConfig(
    const QVariantMap& config) const {
    const QVariant value = config.value(QStringLiteral("format"));
    if (value.isValid() && !value.canConvert<QString>())
        return PluginConfigValidationResult::rejected(
            QStringLiteral("format must be a string"));
    return PluginConfigValidationResult::accepted();
}

xuanhub::PluginConfigApplyResult ClockPlugin::applyConfig(
    const QVariantMap& config) {
    if (!m_vm)
        return PluginConfigApplyResult::rejected(
            QStringLiteral("view model is not initialized"));
    const auto validation = validateConfig(config);
    if (!validation.valid) return PluginConfigApplyResult::rejected(validation.error);
    QString format = config.value(QStringLiteral("format")).toString();
    if (format.isEmpty()) format = QStringLiteral("HH:mm:ss");
    m_vm->setFormat(format);
    return PluginConfigApplyResult::applied();
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
