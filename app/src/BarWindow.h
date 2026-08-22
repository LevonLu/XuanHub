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
class LocaleManager;
class PopupService;
class PluginActionRegistry;

class ThemeManager;

class BarWindow : public QQuickView {
    Q_OBJECT
public:
    explicit BarWindow(ConfigService* config, PluginListModel* model,
                       ThemeManager* theme, LocaleManager* locale,
                       PluginActionRegistry* pluginActions,
                       QWindow* parent = nullptr);
    ~BarWindow() override;

    // 在窗口已 create() 之后调用：按当前模式定位窗口（appbar reserve / taskbar 嵌入）。
    void setupBar();

    // taskbar 模式下 explorer 重启（TaskbarCreated / 任务栏句柄失效）后调用：
    // 我们的 HWND 已随任务栏销毁，重建平台窗口并重新嵌入。非 taskbar 模式直接忽略。
    void recreateForTaskbar();

Q_SIGNALS:
    // bar 收到任意鼠标按下时发射（先于 QML item 投递）。
    // 用途：bar 是 WindowDoesNotAcceptFocus，点击 bar 不会让插件弹窗失活，
    // 弹窗插件（usage_hub 详情、clock 日历）收听此信号实现"点 bar 任意处关闭弹窗"；
    // 点击桌面/其他窗口的关闭仍由弹窗自身的 onActiveChanged 失活逻辑覆盖。
    void barPressed();

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    ConfigService* m_config;
    WorkAreaGuard* m_guard;
    TaskbarEmbed* m_embed;
    PopupService* m_popupService;
};
