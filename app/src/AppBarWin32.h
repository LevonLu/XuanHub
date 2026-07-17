// AppBarWin32.h — Win32 AppBar（SHAppBarMessage）封装
// 负责 reserve / 归还桌面工作区边缘。玄枢用它占住主屏顶部或底部。
// 通过原生事件过滤器响应 ABN_POSCHANGED / ABN_FULLSCREENAPP，
// 以信号形式上报，供窗口层重新布局 / 给全屏应用让层。
#pragma once
#include <QObject>
#include <QWindow>   // WId
#include <QRect>

class AppBarWin32 : public QObject {
    Q_OBJECT
public:
    enum class Edge { Top, Bottom };

    explicit AppBarWin32(QObject* parent = nullptr);
    ~AppBarWin32() override;

    // 注册为指定边缘的 appbar，reserve 高度为 heightPhysPx（物理像素）的条带。
    // 成功后系统会让出对应工作区，最大化窗口不会遮挡。
    // 可重复调用：同 hwnd 时只做参数更新 + 位置重协商（不重新 ABM_NEW）。
    bool registerEdge(WId hwnd, Edge edge, int heightPhysPx);

    // 注销 appbar，归还工作区。析构时自动调用。
    void unregister();

    bool isRegistered() const { return m_registered; }

    // 最近一次 SETPOS 协商后的矩形（物理像素，屏幕绝对坐标）；未注册时为空
    QRect lastRect() const;

signals:
    // ABN_POSCHANGED：任务栏/其他 appbar 变化（含分辨率、DPI 变动），需重新布局
    void needRelayout();
    // ABN_FULLSCREENAPP：全屏应用出现（true）/ 退出（false），bar 应让层/恢复
    void fullscreenActiveChanged(bool active);

private:
    void handleCallback(quintptr wParam, qintptr lParam);
    void renegotiate();   // ABM_QUERYPOS + ABM_SETPOS（矩形未变则跳过，防通知回环）

    class Filter;         // 原生事件过滤器（把 appbar 回调消息转给 handleCallback）
    Filter* m_filter = nullptr;

    bool m_registered = false;
    unsigned int m_callbackMsg = 0;  // appbar 回调消息 ID
    WId m_hwnd = 0;
    Edge m_edge = Edge::Top;
    int m_heightPhys = 0;
    bool m_inCallback = false;       // 防自身 SETPOS 回声重入
    // 上次 SETPOS 的矩形（变化检测用；避免包含 windows.h 的自足 POD）
    struct NativeRect { long left = 0, top = 0, right = 0, bottom = 0; };
    NativeRect m_lastRc{};
};
