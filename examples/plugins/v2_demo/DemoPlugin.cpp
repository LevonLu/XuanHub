// DemoPlugin.cpp — SDK V2 Descriptor、初始化和热配置参考实现
#include "DemoPlugin.h"
#include "barplugin/HostServices.h"

using namespace xuanhub;

PluginDescriptor DemoPlugin::descriptor() const {
    PluginDescriptor value;
    value.id = QStringLiteral("example.v2_demo");
    value.displayName = QStringLiteral("V2 Demo");
    value.version = QStringLiteral("1.0.0");
    value.apiVersion = 2;
    value.minHostVersion = QStringLiteral("0.4.0");
    value.maxHostVersion = QStringLiteral("0.x");
    value.author = QStringLiteral("Example");
    value.entry.backend = QStringLiteral("v2_demo.dll");
    value.entry.qml = QStringLiteral("Demo.qml");
    value.capabilities = {QStringLiteral("barItem")};
    value.compatibility.architecture = QStringLiteral("x64");
    value.compatibility.qtVersion = QStringLiteral("6.9.3");
    value.compatibility.compiler = QStringLiteral("msvc2022");
    value.compatibility.crt = QStringLiteral("dynamic");
    return value;
}

PluginInitResult DemoPlugin::initialize(const PluginContext2& context) {
    if (context.hostServices) {
        context.hostServices->logger()->log(
            PluginLogLevel::Info, QStringLiteral("SDK V2 demo initialized"));
    }
    const auto result = applyConfig(context.pluginConfig);
    return result.disposition == PluginConfigApplyDisposition::Rejected
        ? PluginInitResult::failed(result.error)
        : PluginInitResult::succeeded();
}

PluginConfigValidationResult DemoPlugin::validateConfig(
    const QVariantMap& config) const {
    const QVariant text = config.value(QStringLiteral("text"));
    if (text.isValid() && !text.canConvert<QString>())
        return PluginConfigValidationResult::rejected(
            QStringLiteral("text must be a string"));
    return PluginConfigValidationResult::accepted();
}

PluginConfigApplyResult DemoPlugin::applyConfig(const QVariantMap& config) {
    const auto validation = validateConfig(config);
    if (!validation.valid) return PluginConfigApplyResult::rejected(validation.error);
    const QString next = config.value(QStringLiteral("text"),
                                      QStringLiteral("SDK V2")).toString();
    if (m_text != next) {
        m_text = next;
        Q_EMIT textChanged();
    }
    return PluginConfigApplyResult::applied();
}
