// SettingsController.h — 设置窗口控制器（M8）
// 懒创建独立 QQuickView（普通窗口，非常驻置顶），托盘 "Settings..." 打开；
// 关闭=隐藏不销毁；每次打开 reloadFromConfig() 刷新草稿。
#pragma once
#include <QObject>
#include <QString>
#include <QPointer>

class QQuickView;
class ConfigService;
class ThemeManager;
class SettingsViewModel;

class SettingsController : public QObject {
    Q_OBJECT
public:
    SettingsController(ConfigService* config, ThemeManager* theme,
                       const QString& exeDir, QObject* parent = nullptr);
    ~SettingsController() override;

public slots:
    void open();   // 首次调用懒建窗口；随后 show + raise

private:
    ConfigService* m_config;
    ThemeManager* m_theme;
    QString m_exeDir;
    SettingsViewModel* m_vm = nullptr;   // 与窗口一同懒建（this 父子）
    QPointer<QQuickView> m_window;
};
