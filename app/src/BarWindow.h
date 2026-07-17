// BarWindow.h — 状态栏窗口（QQuickView）
// 无边框、置顶、Tool（不入任务栏/Alt+Tab），定位到主屏顶部并 reserve 工作区。
#pragma once
#include <QQuickView>

class ConfigService;
class WorkAreaGuard;
class PluginListModel;

class ThemeManager;

class BarWindow : public QQuickView {
    Q_OBJECT
public:
    explicit BarWindow(ConfigService* config, PluginListModel* model,
                       ThemeManager* theme, QWindow* parent = nullptr);
    ~BarWindow() override;

    // 在窗口已 create() 之后调用：定位到主屏顶部并 reserve 工作区。
    void setupBar();

private:
    ConfigService* m_config;
    WorkAreaGuard* m_guard;
};
