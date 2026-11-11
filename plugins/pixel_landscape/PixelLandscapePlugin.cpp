// PixelLandscapePlugin.cpp — 朝暮插件生命周期实现
#include "PixelLandscapePlugin.h"
#include "PixelLandscapeViewModel.h"
#include "barplugin/HostServices.h"

xuanhub::PluginDescriptor PixelLandscapePlugin::descriptor() const {
    xuanhub::PluginDescriptor value;
    value.id = QStringLiteral("pixel_landscape");
    value.displayName = QStringLiteral("朝暮");
    value.version = QStringLiteral("0.3.0");
    value.apiVersion = 2;
    value.configVersion = 1;
    value.minHostVersion = QStringLiteral("0.4.0");
    value.maxHostVersion = QStringLiteral("0.x");
    value.author = QStringLiteral("XuanHub");
    value.entry.backend = QStringLiteral("pixel_landscape.dll");
    value.entry.qml = QStringLiteral("PixelLandscape.qml");
    value.entry.settingsQml = QStringLiteral("PixelLandscapeSettings.qml");
    value.capabilities = {QStringLiteral("barItem"), QStringLiteral("settings"), QStringLiteral("popup")};
    value.permissions = {QStringLiteral("storage.private")};
    value.layout.declared = true;
    value.layout.region = QStringLiteral("center");
    value.layout.minWidth = 160;
    value.layout.preferredWidth = 320;
    value.layout.maxWidth = 320;
    value.layout.priority = 10;
    value.layout.overflow = QStringLiteral("hide");
    value.compatibility.architecture = QStringLiteral("x64");
    value.compatibility.qtVersion = QStringLiteral("6.9.3");
    value.compatibility.compiler = QStringLiteral("msvc2022");
    value.compatibility.crt = QStringLiteral("dynamic");
    return value;
}

xuanhub::PluginInitResult PixelLandscapePlugin::initialize(const xuanhub::PluginContext2& context) {
    if (m_viewModel) return xuanhub::PluginInitResult::failed(QStringLiteral("already initialized"));
    const auto validation = validateConfig(context.pluginConfig);
    if (!validation.valid) return xuanhub::PluginInitResult::failed(validation.error);
    m_viewModel = new PixelLandscapeViewModel;
    if (context.hostServices && context.hostServices->hasPermission(QStringLiteral("storage.private")))
        m_viewModel->setStorage(context.hostServices->storage());
    m_viewModel->applyConfig(context.pluginConfig);
    m_viewModel->start();
    return xuanhub::PluginInitResult::succeeded();
}

xuanhub::PluginConfigValidationResult PixelLandscapePlugin::validateConfig(const QVariantMap& config) const {
    const QString error = PixelLandscapeViewModel::configurationError(config);
    return error.isEmpty() ? xuanhub::PluginConfigValidationResult::accepted()
                          : xuanhub::PluginConfigValidationResult::rejected(error);
}

xuanhub::PluginConfigApplyResult PixelLandscapePlugin::applyConfig(const QVariantMap& config) {
    if (!m_viewModel) return xuanhub::PluginConfigApplyResult::rejected(QStringLiteral("not initialized"));
    const auto validation = validateConfig(config);
    if (!validation.valid) return xuanhub::PluginConfigApplyResult::rejected(validation.error);
    m_viewModel->applyConfig(config);
    return xuanhub::PluginConfigApplyResult::applied();
}

QObject* PixelLandscapePlugin::viewModel() { return m_viewModel; }
QUrl PixelLandscapePlugin::qmlUrl() const { return QUrl(QStringLiteral("PixelLandscape.qml")); }

void PixelLandscapePlugin::shutdown() {
    if (m_viewModel) m_viewModel->stop();
    delete m_viewModel;
    m_viewModel = nullptr;
}
