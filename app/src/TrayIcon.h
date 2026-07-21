// TrayIcon.h — 系统托盘（QSystemTrayIcon + 菜单 + 开机自启 + app.notify 气泡提示）
#pragma once
#include <QObject>
#include <QVariantMap>

namespace xuanhub { class EventBus; }
class ThemeManager;
class ConfigService;
class QSystemTrayIcon;
class QMenu;
class QAction;

class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(ThemeManager* theme, xuanhub::EventBus* bus,
                      ConfigService* config, QObject* parent = nullptr);

    void show();
    void recreate();               // Explorer 重启后重建托盘图标

signals:
    void settingsRequested();    // 菜单 "Settings..." 触发（M8 设置窗口）
    void reloadRequested();      // 菜单 "Reload Config" 触发（M7 热重载）

private slots:
    void setAutoStartEnabled(bool on);
    void setTaskbarMode(bool on);   // 菜单 "Taskbar Mode"：写回 config.json 走热重载生效
    void refreshIcon();             // 主题变化时重绘图标（连 ThemeManager::themeChanged）
    void onAppNotify(QVariantMap payload);  // EventBus "app.notify"：插件请求的气泡提示

private:
    QSystemTrayIcon* m_tray = nullptr;
    ThemeManager*    m_theme;
    ConfigService*   m_config;
    QMenu*           m_menu = nullptr;
    QAction*         m_autoStartAction = nullptr;
    QAction*         m_taskbarModeAction = nullptr;
};
