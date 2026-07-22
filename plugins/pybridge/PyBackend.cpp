// PyBackend.cpp — pybackend.dll 导出实现：解释器初始化 + 子插件 VM 工厂
// 本 DLL 是进程中唯一触碰 CPython/nanobind 的模块（静态导入 python3.dll），
// 拆分原因见 PyBackendApi.h 头注释。
#define XUANHUB_PYBACKEND_BUILD
#include "PyBackendApi.h"
#include "PyProxyViewModel.h"

#include <QDebug>
#include <nanobind/nanobind.h>

namespace nb = nanobind;

bool xb_py_initialize() {
    // 解释器是进程级单例，任何时刻不主动 finalize（决策见 PyBridgePlugin.cpp）；
    // 热重载（shutdownAll → loadFrom）会再次进入，幂等返回
    static bool s_ready = false;
    if (s_ready) return true;
    Py_Initialize();
    if (!Py_IsInitialized()) {
        qWarning() << "[PyBridge] Py_Initialize failed";
        return false;
    }
    // 关键：初始化后主线程默认持有 GIL，而它在 Qt 事件循环里从不进入
    // 解释器求值循环，永不主动让出 —— python 后台线程会饿死
    //（实测 worker 3 秒只跑到 1 次）。此处永久 release（有意泄漏，
    // 进程生命周期内不恢复）；此后一切 python 调用一律 gil_scoped_acquire。
    static auto* s_gilRelease = new nb::gil_scoped_release();
    Q_UNUSED(s_gilRelease);
    s_ready = true;
    qInfo().noquote() << "[PyBridge] python" << Py_GetVersion() << "initialized";
    return true;
}

QObject* xb_py_vm_load(const QString& id, const QString& dirPath,
                       const QVariantMap& cfg, QObject* parent) {
    auto* vm = new PyProxyViewModel(id, dirPath, cfg, parent);
    if (!vm->load()) {
        delete vm;  // 脚本语法错误/缺 update()：记日志后跳过，不影响宿主与其他插件
        return nullptr;
    }
    return vm;
}
