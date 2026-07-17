// WorkAreaGuard.cpp
#include "WorkAreaGuard.h"
#include "AppBarWin32.h"

WorkAreaGuard::WorkAreaGuard(QObject* parent)
    : QObject(parent), m_appbar(new AppBarWin32(this)) {}

WorkAreaGuard::~WorkAreaGuard() { release(); }

bool WorkAreaGuard::reserveTop(WId hwnd, int heightPhysPx) {
    return m_appbar->registerTop(hwnd, heightPhysPx);
}

void WorkAreaGuard::release() {
    if (m_appbar) m_appbar->unregister();
}
