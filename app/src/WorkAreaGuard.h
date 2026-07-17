// WorkAreaGuard.h — appbar 生命周期守卫
// 负责启动清理 / 退出归还工作区的策略层，封装 AppBarWin32。
#pragma once
#include <QObject>
#include <QWindow>   // WId

class AppBarWin32;

class WorkAreaGuard : public QObject {
    Q_OBJECT
public:
    explicit WorkAreaGuard(QObject* parent = nullptr);
    ~WorkAreaGuard() override;

    // reserve 主屏顶部，hwnd 由 QWindow::winId() 提供，heightPhysPx 为物理像素。
    bool reserveTop(WId hwnd, int heightPhysPx);

    // 归还工作区（析构时自动调用）。
    void release();

private:
    AppBarWin32* m_appbar;
};
