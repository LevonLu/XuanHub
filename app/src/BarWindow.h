// BarWindow.h — 状态栏窗口（QQuickView）
// 无边框、置顶、Tool（不入任务栏/Alt+Tab）。两种工作模式（config bar.mode）：
// appbar  —— 定位到主屏顶部/底部并 reserve 工作区（WorkAreaGuard）；
// taskbar —— SetParent 嵌入原生任务栏左侧（TaskbarEmbed），背景透明不占工作区。
#pragma once
#include <QQuickView>

class ConfigService;
class WorkAreaGuard;
class TaskbarEmbed;
class PluginListModel;

class ThemeManager;

class BarWindow : public QQuickView {
    Q_OBJECT
public:
    explicit BarWindow(ConfigService* config, PluginListModel* model,
                       ThemeManager* theme, QWindow* parent = nullptr);
    ~BarWindow() override;

    // 在窗口已 create() 之后调用：按当前模式定位窗口（appbar reserve / taskbar 嵌入）。
    void setupBar();

    // taskbar 模式下 explorer 重启（TaskbarCreated / 任务栏句柄失效）后调用：
    // 我们的 HWND 已随任务栏销毁，重建平台窗口并重新嵌入。非 taskbar 模式直接忽略。
    void recreateForTaskbar();

private:
    ConfigService* m_config;
    WorkAreaGuard* m_guard;
    TaskbarEmbed* m_embed;
};
