// SettingsViewModel.cpp
#include "SettingsViewModel.h"
#include "AutoStart.h"
#include "ConfigService.h"
#include "PluginListModel.h"
#include "SettingsPluginModel.h"
#include "SettingsPluginProxy.h"
#include "SettingsPluginSortModel.h"
#include "ThemeManager.h"
#include "ThemeRegistry.h"
#include "LocaleManager.h"

#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

SettingsViewModel::SettingsViewModel(ConfigService* config, ThemeRegistry* registry,
                                     ThemeManager* themeMgr, LocaleManager* locale,
                                     QString exeDir,
                                     PluginListModel* barPlugins, QObject* parent)
    : QObject(parent), m_config(config), m_registry(registry), m_themeMgr(themeMgr),
      m_locale(locale), m_exeDir(std::move(exeDir)), m_barPlugins(barPlugins),
      m_pluginModel(new SettingsPluginModel(this)),
      m_sortModel(new SettingsPluginSortModel(this)),
      m_pluginProxy(new SettingsPluginProxy(m_pluginModel, this)) {
    m_sortModel->setSourceModel(m_pluginModel);
    m_sortModel->sort(0);   // sortColumn 置 0 启用排序；dynamicSortFilter 后续自动维持
    if (m_locale) {
        connect(m_locale, &LocaleManager::localeChanged, this, [this] {
            m_pluginModel->retranslate();
            setErrorMessage({});
        });
    }
    if (m_barPlugins) {
        connect(m_barPlugins, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex& first, const QModelIndex& last) {
            for (int row = first.row(); row <= last.row(); ++row) {
                const QString id = m_barPlugins->data(
                    m_barPlugins->index(row), PluginListModel::IdRole).toString();
                if (!id.isEmpty()) emit pluginHealthChanged(id);
            }
        });
        connect(m_barPlugins, &QAbstractItemModel::modelReset, this, [this] {
            emit pluginHealthChanged({});
        });
    }
}

QAbstractItemModel* SettingsViewModel::pluginModel() const { return m_sortModel; }

QObject* SettingsViewModel::pluginProxy() const { return m_pluginProxy; }

int SettingsViewModel::pluginContentWidth(const QString& id) const {
    return m_barPlugins ? m_barPlugins->contentWidth(id) : 0;
}

QVariantMap SettingsViewModel::pluginHealth(const QString& id) const {
    return m_barPlugins ? m_barPlugins->health(id) : QVariantMap{};
}

bool SettingsViewModel::restartPlugin(const QString& id) {
    return m_barPlugins && m_barPlugins->restartPlugin(id);
}

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

void SettingsViewModel::setBarMode(const QString& v) {
    if (m_barMode == v) return;
    m_barMode = v;
    emit barModeChanged();
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

void SettingsViewModel::setLanguage(const QString& v) {
    const QString normalized = LocaleManager::normalizeRequested(v);
    if (m_language == normalized) return;
    m_language = normalized;
    emit languageChanged();
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
    setBarMode(m_config->barMode());
    // 主题草稿取运行时值（ThemeManager）而非文件旧值（ConfigService）：
    // 托盘「切换深色/浅色」是纯运行时切换不写文件，若此处读文件旧值，用户改了别的设置
    // 保存时会把草稿里的旧 mode 一并写回 → 热重载把用户临时切的模式打回文件值。
    setTheme(m_themeMgr ? m_themeMgr->mode() : m_config->theme());
    setThemeId(m_themeMgr ? m_themeMgr->themeId() : m_config->themeId());
    setAutoStart(autostart::isEnabled());
    setLanguage(m_config->language());
    m_pluginModel->reload(m_config->pluginEntries(),
                          m_config->rawConfig().value(QStringLiteral("plugins")).toArray(),
                          m_exeDir, m_config->activePath());
    // 快照配置指纹：窗口打开期间的草稿以此为底，apply 前据此判定外部改动
    m_openFingerprint = currentFingerprint();
    setErrorMessage({});
}

QString SettingsViewModel::currentFingerprint() const {
    // 只取本窗口会写回的内容：ui.language + bar 段已知键 + plugins 数组原始 JSON（含手写未知键，
    // 外部只改未知键同样算外部改动）。bar 未知键（如 monitor）本窗口不动，不入指纹
    const QJsonObject root = m_config->rawConfig();
    const QJsonObject bar = root.value(QStringLiteral("bar")).toObject();
    QJsonObject barKeys;
    for (const char* k : {"height", "edge", "mode", "opacity", "theme", "themeId"})
        barKeys.insert(QString::fromLatin1(k), bar.value(QString::fromLatin1(k)));
    QJsonObject fp;
    QJsonObject uiKeys;
    const QJsonObject ui = root.value(QStringLiteral("ui")).toObject();
    uiKeys.insert(QStringLiteral("language"), ui.value(QStringLiteral("language")));
    fp[QStringLiteral("ui")] = uiKeys;
    fp[QStringLiteral("bar")] = barKeys;
    fp[QStringLiteral("plugins")] = root.value(QStringLiteral("plugins")).toArray();
    return QString::fromUtf8(QJsonDocument(fp).toJson(QJsonDocument::Compact));
}

bool SettingsViewModel::apply() {
    // 草稿过期保护：窗口打开后配置被外部改动（热重载/手动编辑）则拒绝保存——
    // 草稿是打开时的快照，继续保存会用旧草稿把外部改动整体覆盖、静默回滚
    if (currentFingerprint() != m_openFingerprint) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors",
            "配置已被外部修改（热重载/手动编辑），请关闭设置窗口重开后再保存"));
        return false;
    }
    // 校验（QML 控件已限范围，这里双保险——草稿也可能被外部代码改动）
    if (m_barHeight < 24 || m_barHeight > 200) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "状态栏高度需在 24–200 之间"));
        return false;
    }
    if (m_barEdge != QStringLiteral("top") && m_barEdge != QStringLiteral("bottom")) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "停靠边缘仅支持 top / bottom"));
        return false;
    }
    if (m_barMode != QStringLiteral("appbar") && m_barMode != QStringLiteral("taskbar")) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "工作模式仅支持 appbar / taskbar"));
        return false;
    }
    if (m_theme != QStringLiteral("dark") && m_theme != QStringLiteral("light")) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "主题模式仅支持 dark / light"));
        return false;
    }
    if (m_language != QStringLiteral("system") && m_language != QStringLiteral("zh-CN")
        && m_language != QStringLiteral("en-US")) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "界面语言配置无效"));
        return false;
    }
    // 校验主题包存在（防 config 手写错 id）
    if (m_registry && m_registry->find(m_themeId) == nullptr) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "主题包不存在：") + m_themeId);
        return false;
    }
    QJsonArray plugins;
    QString err;
    if (m_pluginModel->isDirty()) {
        if (!m_pluginModel->toPluginEntriesJson(&plugins, &err)) {
            setErrorMessage(err);
            return false;
        }
    } else {
        // 插件页没有实际编辑时保留原始数组。模型加载阶段会为旧中文枚举建立
        // 稳定值草稿，但语言/主题保存不应顺带写回这些迁移并触发插件全量重载。
        plugins = m_config->rawConfig().value(QStringLiteral("plugins")).toArray();
    }
    // 只改已知键：基于 rawConfig 组装，未知键（如 bar.monitor）原样保留
    QJsonObject root = m_config->rawConfig();
    QJsonObject bar = root.value(QStringLiteral("bar")).toObject();
    bar[QStringLiteral("height")] = m_barHeight;
    bar[QStringLiteral("edge")] = m_barEdge;
    bar[QStringLiteral("mode")] = m_barMode;     // 工作模式 appbar/taskbar（M11）
    bar[QStringLiteral("opacity")] = m_barOpacity / 100.0;   // int 0-100 → qreal 0.0-1.0
    bar[QStringLiteral("theme")] = m_theme;          // 模式 dark/light
    bar[QStringLiteral("themeId")] = m_themeId;      // 主题包 id
    root[QStringLiteral("bar")] = bar;
    QJsonObject ui = root.value(QStringLiteral("ui")).toObject();
    ui[QStringLiteral("language")] = m_language;
    root[QStringLiteral("ui")] = ui;
    root[QStringLiteral("plugins")] = plugins;
    if (!m_config->save(root)) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "写入 config.json 失败（权限/磁盘，详见日志）"));
        return false;
    }
    autostart::setEnabled(m_autoStart);
    // 更新指纹到刚保存的状态：本次保存触发的热重载不算外部修改
    //（save() 已把 m_raw 更新为同一内容，此处重算即新快照）
    m_openFingerprint = currentFingerprint();
    setErrorMessage({});
    qDebug() << "[Settings] applied:"
             << "height=" << m_barHeight << "edge=" << m_barEdge
             << "barMode=" << m_barMode
             << "themeId=" << m_themeId << "theme=" << m_theme
             << "language=" << m_language
             << "autoStart=" << m_autoStart << "plugins=" << plugins.size();
    return true;
}

void SettingsViewModel::setErrorMessage(const QString& m) {
    if (m_errorMessage == m) return;
    m_errorMessage = m;
    emit errorMessageChanged();
}
