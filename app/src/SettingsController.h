// SettingsController.h — 设置窗口控制器（M8）
// 懒创建独立 QQuickView（普通窗口，非常驻置顶），托盘 "设置..." 打开；
// 关闭=隐藏不销毁；每次打开 reloadFromConfig() 刷新草稿。
// 调试钩子：--uitest 启动参数开启 devUiEvent()（Qt 层注入鼠标/键盘事件，
// 供无头自动化验收用；正常启动完全不可见，QML 侧也没有 uiTestCommands）。
#pragma once
#include <QObject>
#include <QString>
#include <QPointer>

class QQuickView;
class ConfigService;
class ThemeManager;
class ThemeRegistry;
class SettingsViewModel;

class SettingsController : public QObject {
    Q_OBJECT
public:
    SettingsController(ConfigService* config, ThemeManager* theme, ThemeRegistry* registry,
                       const QString& exeDir, QObject* parent = nullptr);
    ~SettingsController() override;

public slots:
    void open();   // 首次调用懒建窗口；随后 show + raise

    // devUiEvent(spec)：仅 --uitest 模式生效。spec 形如
    // "click:x,y"（窗口本地坐标，先移真光标再投 Qt 鼠标事件，含 hover/焦点语义）
    // 或 "kp_<text>"（向当前焦点项投按键，如 kp_x / kp_enter）。
    Q_INVOKABLE void devUiEvent(const QString& spec);

private:
    ConfigService* m_config;
    ThemeManager* m_theme;
    ThemeRegistry* m_registry;   // M9：主题包枚举（设置窗口主题下拉）
    QString m_exeDir;
    SettingsViewModel* m_vm = nullptr;   // 与窗口一同懒建（this 父子）
    QPointer<QQuickView> m_window;
    bool m_uitest = false;               // --uitest 启动参数置位
};
