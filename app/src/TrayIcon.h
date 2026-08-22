// TrayIcon.h — 系统托盘（QSystemTrayIcon + 菜单 + 开机自启 + app.notify 气泡提示）
#pragma once
#include <QObject>
#include <QVariantMap>

namespace xuanhub { class EventBus; }
class ThemeManager;
class ConfigService;
class LocaleManager;
class QSystemTrayIcon;
class QMenu;
class QAction;

class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(ThemeManager* theme, xuanhub::EventBus* bus,
                      ConfigService* config, LocaleManager* locale,
                      QObject* parent = nullptr);
    ~TrayIcon() override;   // m_menu 无父对象（setContextMenu 不接管所有权），需手动释放

    void show();
    void recreate();               // Explorer 重启后重建托盘图标

signals:
    void settingsRequested();    // 菜单 "设置..." 触发（M8 设置窗口）
    void reloadRequested();      // 菜单 "重新加载配置" 触发（M7 热重载）

private slots:
    void setAutoStartEnabled(bool on);
    void setTaskbarMode(bool on);   // 菜单 "任务栏模式"：写回 config.json 走热重载生效
    void refreshIcon();             // 主题变化时重绘图标（连 ThemeManager::themeChanged）
    void onAppNotify(QVariantMap payload);  // EventBus "app.notify"：插件请求的气泡提示
    void onQuickMenuCommand(QVariantMap payload);  // bar 菜单插件复用托盘动作
    void publishQuickMenuState(QVariantMap payload = {});  // 向插件回传当前勾选状态
    void retranslate();

private:
    QSystemTrayIcon* m_tray = nullptr;
    ThemeManager*    m_theme;
    ConfigService*   m_config;
    xuanhub::EventBus* m_bus;
    LocaleManager*    m_locale;
    QMenu*           m_menu = nullptr;
    QAction*         m_autoStartAction = nullptr;
    QAction*         m_taskbarModeAction = nullptr;
    QAction*         m_settingsAction = nullptr;
    QAction*         m_toggleThemeAction = nullptr;
    QAction*         m_reloadAction = nullptr;
    QAction*         m_quitAction = nullptr;
};
