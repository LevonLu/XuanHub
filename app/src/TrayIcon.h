// TrayIcon.h — 系统托盘（QSystemTrayIcon + 菜单 + 开机自启）
#pragma once
#include <QObject>

class ThemeManager;
class QSystemTrayIcon;
class QMenu;
class QAction;

class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(ThemeManager* theme, QObject* parent = nullptr);

    void show();
    void recreate();               // Explorer 重启后重建托盘图标

private slots:
    void setAutoStartEnabled(bool on);

private:
    QSystemTrayIcon* m_tray = nullptr;
    ThemeManager*    m_theme;
    QMenu*           m_menu = nullptr;
    QAction*         m_autoStartAction = nullptr;
};
