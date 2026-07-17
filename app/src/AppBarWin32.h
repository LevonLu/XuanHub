// AppBarWin32.h — Win32 AppBar（SHAppBarMessage）封装
// 负责 reserve / 归还桌面工作区边缘。玄枢用它占住主屏顶部。
#pragma once
#include <QObject>
#include <QWindow>   // WId

class AppBarWin32 : public QObject {
    Q_OBJECT
public:
    explicit AppBarWin32(QObject* parent = nullptr);
    ~AppBarWin32() override;

    // 注册为顶部 appbar，reserve 高度为 heightPhysPx（物理像素）的顶部条带。
    // 成功后系统会让出对应工作区，最大化窗口不会遮挡。
    // 返回是否注册成功。
    bool registerTop(WId hwnd, int heightPhysPx);

    // 注销 appbar，归还工作区。析构时自动调用。
    void unregister();

    bool isRegistered() const { return m_registered; }

private:
    bool m_registered = false;
    unsigned int m_callbackMsg = 0;  // appbar 回调消息 ID
    WId m_hwnd = 0;
};
