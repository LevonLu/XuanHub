// PyBackendApi.h — pybridge.dll ↔ pybackend.dll 的窄接口（仅函数，无数据符号）
// 为什么拆两个 DLL：nanobind 以数据符号（PyBool_Type 等）导入 python3.dll，
// 而 /DELAYLOAD 只支持函数导入（LNK1194），python3.dll 又必须延迟到 init 按
// 发现的目录预装之后才能解析。于是把「碰 Python 的代码」整体收进
// pybackend.dll（静态导入 python3.dll，放 exe 旁），pybridge.dll 只经这组
// 函数间接调用，/DELAYLOAD:pybackend.dll 延迟到环境确认 + 预装之后。
#pragma once
#include <QtCore/qglobal.h>
#include <QString>
#include <QVariantMap>

class QObject;

#if defined(XUANHUB_PYBACKEND_BUILD)
#  define XB_BACKEND_API Q_DECL_EXPORT
#else
#  define XB_BACKEND_API Q_DECL_IMPORT
#endif

extern "C" {

// 初始化解释器（Py_Initialize + 主线程永久释放 GIL）；进程内幂等。
// 调用前必须已按发现目录预装 python3XX.dll / python3.dll（PyBridgePlugin 负责）
XB_BACKEND_API bool xb_py_initialize();

// 创建并加载一个 Python 子插件代理 VM；脚本错误/缺 update() 返回 nullptr。
// 生命周期归 parent（PyBridgePlugin），shutdown 时随 parent 销毁
XB_BACKEND_API QObject* xb_py_vm_load(const QString& id, const QString& dirPath,
                                      const QVariantMap& cfg, QObject* parent);
}
