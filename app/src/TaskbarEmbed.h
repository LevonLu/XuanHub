// TaskbarEmbed.h — 任务栏嵌入（SetParent 到 Shell_TrayWnd）
// taskbar 工作模式下，把 BarWindow 变成任务栏的子窗口，实现与原生任务栏融为一体：
// 任务栏移动 / 自动隐藏 / 被全屏应用遮挡时天然跟随，无需自己处理 z-order。
// 方案先例：TrafficMonitor 等工具长期使用该手法（Win10/11 均可用）。
// 定位用 500ms 轮询矫正（任务栏布局变化会推挤/遮盖子窗口）；
// TrafficMonitor 的 ReBarWindow32/MSTaskSwWClass 尺寸 hook 属于优化，轮询已够用。
#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>

class QWindow;

class TaskbarEmbed : public QObject {
    Q_OBJECT
public:
    explicit TaskbarEmbed(QObject* parent = nullptr);

    // 把 win 嵌入主屏任务栏左侧；offsetXLogical 为距任务栏左缘的偏移（逻辑像素）。
    // 幂等：已 attach 时重复调用只做参数更新 + 重新定位（热重载路径会反复走到）。
    bool attach(QWindow* win, int offsetXLogical);

    // 脱离任务栏，恢复为独立置顶 popup（切回 appbar 模式用）。
    void detach();

    bool isAttached() const { return !m_win.isNull(); }

    // QML 内容实际宽度（逻辑像素）；变化时重新定位（插件增删/时钟数字变宽时宽度跟随）
    void setContentWidth(int wLogical);

    // 按任务栏矩形重算目标位置并 SetWindowPos（顺带置顶到兄弟窗口之上）
    void relayout();

signals:
    // 任务栏 HWND 失效（explorer 重启，子窗口随父销毁），调用方需重建窗口后重新 attach
    void taskbarVanished();

private:
    void poll();   // 轮询矫正：任务栏矩形变化 / 自身被压底 / 任务栏失效检测

    QPointer<QWindow> m_win;
    QTimer m_timer;
    void* m_trayHwnd = nullptr;  // HWND，头文件不引入 windows.h
    int m_offsetX = 0;           // 逻辑像素
    int m_contentWidth = 0;      // 逻辑像素
};
