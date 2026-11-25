// MusicPlugin.cpp — 音乐 生命周期实现
#include "MusicPlugin.h"
#include "MusicViewModel.h"

xuanhub::PluginDescriptor MusicPlugin::descriptor() const {
    xuanhub::PluginDescriptor value;
    value.id = QStringLiteral("music");
    value.displayName = QStringLiteral("音乐");
    value.version = QStringLiteral("0.1.0");
    value.apiVersion = 2;
    value.configVersion = 1;
    value.minHostVersion = QStringLiteral("0.5.0");
    value.maxHostVersion = QStringLiteral("0.x");
    value.author = QStringLiteral("XuanHub");
    value.entry.backend = QStringLiteral("music.dll");
    value.entry.qml = QStringLiteral("Music.qml");
    value.capabilities = {QStringLiteral("barItem"), QStringLiteral("settings"), QStringLiteral("popup")};
    value.layout.declared = true;
    value.layout.region = QStringLiteral("center");
    value.layout.minWidth = 80;
    value.layout.preferredWidth = 240;
    value.layout.maxWidth = 240;
    value.layout.priority = 30;
    value.layout.overflow = QStringLiteral("hide");
    value.compatibility.architecture = QStringLiteral("x64");
    value.compatibility.qtVersion = QStringLiteral("6.9.3");
    value.compatibility.compiler = QStringLiteral("msvc2022");
    value.compatibility.crt = QStringLiteral("dynamic");
    return value;
}

xuanhub::PluginInitResult MusicPlugin::initialize(const xuanhub::PluginContext2& context) {
    const auto validation = validateConfig(context.pluginConfig);
    if (!validation.valid) return xuanhub::PluginInitResult::failed(validation.error);
    m_viewModel = new MusicViewModel;
    applyConfig(context.pluginConfig);
    m_viewModel->start();
    return xuanhub::PluginInitResult::succeeded();
}

xuanhub::PluginConfigValidationResult MusicPlugin::validateConfig(const QVariantMap& config) const {
    const auto mode = config.value(QStringLiteral("lineMode"), QStringLiteral("single"));
    if (mode != QStringLiteral("single") && mode != QStringLiteral("double"))
        return xuanhub::PluginConfigValidationResult::rejected(QStringLiteral("无效的歌词行数"));
    const auto font = config.value(QStringLiteral("doubleLineFont"), QStringLiteral("emphasis"));
    if (font != QStringLiteral("emphasis") && font != QStringLiteral("uniform"))
        return xuanhub::PluginConfigValidationResult::rejected(QStringLiteral("无效的双行字号选项"));
    return xuanhub::PluginConfigValidationResult::accepted();
}

xuanhub::PluginConfigApplyResult MusicPlugin::applyConfig(const QVariantMap& config) {
    if (!m_viewModel) return xuanhub::PluginConfigApplyResult::rejected(QStringLiteral("插件尚未初始化"));
    const auto validation = validateConfig(config);
    if (!validation.valid) return xuanhub::PluginConfigApplyResult::rejected(validation.error);
    m_viewModel->setTwoLines(config.value(QStringLiteral("lineMode")).toString() == QStringLiteral("double"));
    m_viewModel->setUniformLineFont(config.value(QStringLiteral("doubleLineFont")).toString()
                                    == QStringLiteral("uniform"));
    return xuanhub::PluginConfigApplyResult::applied();
}

QObject* MusicPlugin::viewModel() { return m_viewModel; }
QUrl MusicPlugin::qmlUrl() const { return QUrl(QStringLiteral("Music.qml")); }

void MusicPlugin::shutdown() {
    delete m_viewModel;
    m_viewModel = nullptr;
}
