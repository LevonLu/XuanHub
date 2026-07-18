// SettingsViewModel.cpp
#include "SettingsViewModel.h"
#include "AutoStart.h"
#include "ConfigService.h"
#include "SettingsPluginModel.h"
#include "SettingsPluginProxy.h"
#include "SettingsPluginSortModel.h"
#include "ThemeRegistry.h"

#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

SettingsViewModel::SettingsViewModel(ConfigService* config, ThemeRegistry* registry,
                                     QString exeDir, QObject* parent)
    : QObject(parent), m_config(config), m_registry(registry), m_exeDir(std::move(exeDir)),
      m_pluginModel(new SettingsPluginModel(this)),
      m_sortModel(new SettingsPluginSortModel(this)),
      m_pluginProxy(new SettingsPluginProxy(m_pluginModel, this)) {
    m_sortModel->setSourceModel(m_pluginModel);
    m_sortModel->sort(0);   // sortColumn 置 0 启用排序；dynamicSortFilter 后续自动维持
}

QAbstractItemModel* SettingsViewModel::pluginModel() const { return m_sortModel; }

QObject* SettingsViewModel::pluginProxy() const { return m_pluginProxy; }

QStringList SettingsViewModel::themePackNames() const {
    return m_registry ? m_registry->packNames() : QStringList{};
}

QStringList SettingsViewModel::themePackIds() const {
    if (!m_registry) return {};
    QStringList ids;
    ids.reserve(m_registry->packs().size());
    for (const auto& p : m_registry->packs()) ids.append(p.id);
    return ids;
}

void SettingsViewModel::setBarHeight(int v) {
    if (m_barHeight == v) return;
    m_barHeight = v;
    emit barHeightChanged();
}

void SettingsViewModel::setBarOpacity(int v) {
    // 钳制到 20-100（下限 20 防 bar 完全看不见）
    v = qBound(20, v, 100);
    if (m_barOpacity == v) return;
    m_barOpacity = v;
    emit barOpacityChanged();
}

void SettingsViewModel::setBarEdge(const QString& v) {
    if (m_barEdge == v) return;
    m_barEdge = v;
    emit barEdgeChanged();
}

void SettingsViewModel::setTheme(const QString& v) {
    if (m_theme == v) return;
    m_theme = v;
    emit themeChanged();
}

void SettingsViewModel::setThemeId(const QString& v) {
    if (m_themeId == v) return;
    m_themeId = v;
    emit themeIdChanged();
}

void SettingsViewModel::setAutoStart(bool v) {
    if (m_autoStart == v) return;
    m_autoStart = v;
    emit autoStartChanged();
}

void SettingsViewModel::reloadThemePacks() {
    // 真正扫盘（用户可能新装/卸载了主题包），再发 NOTIFY 让 QML 重新读取 names/ids
    if (m_registry)
        m_registry->reload(QDir(m_exeDir).absoluteFilePath(QStringLiteral("themes")));
    emit themePacksReloaded();
}

void SettingsViewModel::reloadFromConfig() {
    setBarHeight(m_config->barHeight());
    // config 存 0.0-1.0 qreal，UI 用 0-100 int（四舍五入）
    setBarOpacity(qRound(m_config->barOpacity() * 100));
    setBarEdge(m_config->barEdge());
    setTheme(m_config->theme());
    setThemeId(m_config->themeId());
    setAutoStart(autostart::isEnabled());
    m_pluginModel->reload(m_config->pluginEntries(), m_exeDir);
    setErrorMessage({});
}

bool SettingsViewModel::apply() {
    // 校验（QML 控件已限范围，这里双保险——草稿也可能被外部代码改动）
    if (m_barHeight < 24 || m_barHeight > 200) {
        setErrorMessage(QStringLiteral("状态栏高度需在 24–200 之间"));
        return false;
    }
    if (m_barEdge != QStringLiteral("top") && m_barEdge != QStringLiteral("bottom")) {
        setErrorMessage(QStringLiteral("停靠边缘仅支持 top / bottom"));
        return false;
    }
    if (m_theme != QStringLiteral("dark") && m_theme != QStringLiteral("light")) {
        setErrorMessage(QStringLiteral("主题模式仅支持 dark / light"));
        return false;
    }
    // 校验主题包存在（防 config 手写错 id）
    if (m_registry && m_registry->find(m_themeId) == nullptr) {
        setErrorMessage(QStringLiteral("主题包不存在：") + m_themeId);
        return false;
    }
    QJsonArray plugins;
    QString err;
    if (!m_pluginModel->toPluginEntriesJson(&plugins, &err)) {
        setErrorMessage(err);
        return false;
    }
    // 只改已知键：基于 rawConfig 组装，未知键（如 bar.monitor）原样保留
    QJsonObject root = m_config->rawConfig();
    QJsonObject bar = root.value(QStringLiteral("bar")).toObject();
    bar[QStringLiteral("height")] = m_barHeight;
    bar[QStringLiteral("edge")] = m_barEdge;
    bar[QStringLiteral("opacity")] = m_barOpacity / 100.0;   // int 0-100 → qreal 0.0-1.0
    bar[QStringLiteral("theme")] = m_theme;          // 模式 dark/light
    bar[QStringLiteral("themeId")] = m_themeId;      // 主题包 id
    root[QStringLiteral("bar")] = bar;
    root[QStringLiteral("plugins")] = plugins;
    if (!m_config->save(root)) {
        setErrorMessage(QStringLiteral("写入 config.json 失败（权限/磁盘，详见日志）"));
        return false;
    }
    autostart::setEnabled(m_autoStart);
    setErrorMessage({});
    qDebug() << "[Settings] applied:"
             << "height=" << m_barHeight << "edge=" << m_barEdge
             << "themeId=" << m_themeId << "mode=" << m_theme
             << "autoStart=" << m_autoStart << "plugins=" << plugins.size();
    return true;
}

void SettingsViewModel::setErrorMessage(const QString& m) {
    if (m_errorMessage == m) return;
    m_errorMessage = m;
    emit errorMessageChanged();
}
