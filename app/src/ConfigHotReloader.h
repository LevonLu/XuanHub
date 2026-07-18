// ConfigHotReloader.h — 配置热重载（M7）
// QFileSystemWatcher 监听 ConfigService::activePath()（300ms 防抖），
// 托盘菜单 "Reload Config" 手动触发走同一 reload() 入口。
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
    void reload();   // 防抖定时器 / 托盘菜单共用入口

private:
    void armWatcher();   // （重新）挂上对 activePath 的监视

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
};
