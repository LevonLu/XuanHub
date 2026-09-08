// ConfigHotReloader.cpp
#include "ConfigHotReloader.h"

#include "BarWindow.h"
#include "ConfigService.h"
#include "PluginListModel.h"
#include "PluginManager.h"
#include "PluginReconciler.h"
#include <QScopedValueRollback>
#include "ThemeManager.h"
#include "ThemeRegistry.h"
#include "LocaleManager.h"

#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <utility>

ConfigHotReloader::ConfigHotReloader(ConfigService* config, ThemeManager* theme,
                                     ThemeRegistry* registry,
                                     PluginManager* pm, PluginListModel* model,
                                     BarWindow* window, LocaleManager* locale,
                                     QString exeDir, QString configDir, QObject* parent)
    : QObject(parent), m_config(config), m_theme(theme), m_registry(registry),
      m_pm(pm), m_model(model), m_window(window),
      m_locale(locale),
      m_exeDir(std::move(exeDir)), m_configDir(std::move(configDir)) {
    // 防抖：一次保存常触发多个 fileChanged（写内容 + 改属性），合并为一次 reload
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(300);
    connect(&m_debounce, &QTimer::timeout, this, &ConfigHotReloader::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString&) {
        // 编辑器常用「临时文件 + 重命名」保存，原路径的监视会随之失效 → 先重新挂上
        armWatcher();
        m_debounce.start();
    });
    // 文件缺失期退而监视父目录（见 armWatcher）：文件恢复时目录变化触发重挂 + 重载
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString&) {
        armWatcher();
        m_debounce.start();
    });
    armWatcher();
    connect(m_pm, &PluginManager::pluginsAboutToStop, this,
            [this](const QStringList& ids) {
        for (const auto& id : ids) m_model->removePlugin(id);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    connect(m_pm, &PluginManager::configStatesChanged, this, [this] {
        m_config->updatePluginApplyStates(m_pm->configStates());
        if (!m_reloading && m_deferredConfig) m_debounce.start();
    });
    m_config->updatePluginApplyStates(m_pm->configStates());
}

void ConfigHotReloader::armWatcher() {
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    const QString p = m_config->activePath();
    if (p.isEmpty()) return;
    if (QFile::exists(p)) {
        m_watcher.addPath(p);
    } else {
        // 文件缺失（被删/重命名且未即刻重建）：监视父目录兜底，
        // 否则 watcher 全空、此后再改 config 都不会触发热重载（实测失挂路径）
        const QString dir = QFileInfo(p).absolutePath();
        if (QDir(dir).exists())
            m_watcher.addPath(dir);
    }
}

void ConfigHotReloader::reload() { reloadImpl(false); }

void ConfigHotReloader::forceReload() { reloadImpl(true); }

void ConfigHotReloader::reloadImpl(bool force) {
    if (m_reloading) {
        m_forcePending = m_forcePending || force;
        m_debounce.start();
        return;
    }
    QScopedValueRollback<bool> guard(m_reloading, true);
    force = force || m_forcePending;
    m_forcePending = false;
    m_deferredConfig = false;
    m_config->beginRuntimeApply();
    qDebug() << "[Config] reload triggered" << (force ? "(force)" : "");
    // 先重挂监视：即使本次 load 失败提前返回，也不让 watcher 停在失挂状态
    armWatcher();
    // 解析失败（文件缺失/JSON 写坏）→ ConfigService 保留旧值，本次不动任何东西
    if (!m_config->load(m_exeDir, m_configDir)) {
        qWarning() << "[Config] reload failed, keep current settings";
        m_config->finishRuntimeApply(QStringLiteral("配置文件读取或解析失败，保留当前运行状态"));
        return;
    }
    armWatcher();  // 生效路径可能换边（exeDir ↔ configDir）

    // languageChanged 已在 ConfigService::load() 内同步触发；强制重载时即使语言值
    // 未变也重扫插件 i18n 目录，方便开发者更新语言包后从托盘立即验证。
    if (force && m_locale) m_locale->reloadCatalogs();

    // 主题包：重扫 themes/（用户可能新装/卸载了主题包），再按文件值同步当前主题
    if (m_registry) m_registry->reload(QDir(m_exeDir).absoluteFilePath(QStringLiteral("themes")));

    // 主题：文件是事实源，覆盖托盘「切换深色/浅色」的运行时切换（托盘切换不写回文件）
    if (m_theme->themeId() != m_config->themeId()
        || m_theme->mode() != m_config->theme()) {
        m_theme->applyTheme(m_config->themeId(), m_config->theme());
    } else {
        // id/mode 没变也可能只改了当前主题包的 theme.json（registry 刚重扫）→ 重解析色板
        m_theme->refresh();
    }

    const auto nextEntries = m_config->pluginEntries();
    const QString pluginsDir = QDir(m_exeDir).absoluteFilePath(QStringLiteral("plugins"));
    if (force) {
        m_model->setPlugins({});
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        m_pm->shutdownAll();
        // 即使某个 DLL 拒绝卸载，也只恢复已停止的目标，保留其失败诊断。
    }
    m_deferredConfig = PluginReconciler::apply(*m_pm, pluginsDir, nextEntries);
    for (const auto& plugin : m_pm->loaded()) {
        m_model->insertPlugin(plugin); // 已有实例不重建 delegate。
        m_model->updateLayout(plugin.id, plugin.position, plugin.order, plugin.width);
    }
    m_model->setLoadErrors(m_pm->loadErrors());
    m_window->setupBar();
    m_config->updatePluginApplyStates(m_pm->configStates());
    m_config->finishRuntimeApply();
}
