// ConfigHotReloader.h — 配置热重载（M7）
// QFileSystemWatcher 监听 ConfigService::activePath()（300ms 防抖），
// 托盘菜单 "重新加载配置" 手动触发走同一 reload() 入口。
#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <QFileSystemWatcher>

class ConfigService;
class ThemeManager;
class ThemeRegistry;
class PluginManager;
class PluginListModel;
class BarWindow;

class ConfigHotReloader : public QObject {
    Q_OBJECT
public:
    ConfigHotReloader(ConfigService* config, ThemeManager* theme, ThemeRegistry* registry,
                      PluginManager* pm, PluginListModel* model, BarWindow* window,
                      QString exeDir, QString configDir, QObject* parent = nullptr);

public slots:
    void reload();        // watcher/防抖路径：plugins 段未变则跳过插件全量重载
    void forceReload();   // 托盘手动路径：无条件全量重载（插件 DLL 更新后用它）

private:
    void reloadImpl(bool force);
    void armWatcher();   // （重新）挂上对 activePath 的监视；文件缺失时退而监视父目录

    ConfigService* m_config;
    ThemeManager* m_theme;
    ThemeRegistry* m_registry;   // M9：重扫 themes/ 发现新装主题包
    PluginManager* m_pm;
    PluginListModel* m_model;
    BarWindow* m_window;
    QString m_exeDir;
    QString m_configDir;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    // 上次生效的 plugins 段指纹：watcher 触发的 reload 若指纹未变（改的是主题/
    // 高度等无关项）则跳过插件全量重载，不再无谓摧毁插件状态（弹窗、计时、
    // Python 子插件运行时）；托盘手动重载走 forceReload 无条件全量
    QString m_pluginsFingerprint;
};
