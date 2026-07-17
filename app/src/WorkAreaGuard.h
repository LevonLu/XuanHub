// WorkAreaGuard.h — appbar 生命周期守卫
// 负责启动清理 / 退出归还工作区的策略层，封装 AppBarWin32。
#pragma once
#include <QObject>
#include <QWindow>   // WId
#include <QRect>

class AppBarWin32;

class WorkAreaGuard : public QObject {
    Q_OBJECT
public:
    enum class Edge { Top, Bottom };

    explicit WorkAreaGuard(QObject* parent = nullptr);
    ~WorkAreaGuard() override;

    // reserve 主屏指定边缘，hwnd 由 QWindow::winId() 提供，heightPhysPx 为物理像素。
    bool reserve(WId hwnd, Edge edge, int heightPhysPx);

    // 归还工作区（析构时自动调用）。
    void release();

    // 最近一次协商后的矩形（物理像素，屏幕绝对坐标）；未注册时为空
    QRect lastRect() const;

signals:
    void needRelayout();                 // 转发：工作区参数变化，需重新布局
    void fullscreenActiveChanged(bool);  // 转发：全屏应用出现/退出

private:
    AppBarWin32* m_appbar;
};
