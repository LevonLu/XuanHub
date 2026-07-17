// barplugin_export.h — barplugin 共享库符号导出宏
// 编译 barplugin 本体时 CMake 定义 BARPLUGIN_LIBRARY（→ Q_DECL_EXPORT），
// 使用方（宿主 exe / 插件 DLL）未定义（→ Q_DECL_IMPORT）。
#pragma once
#include <QtCore/qglobal.h>

#if defined(BARPLUGIN_LIBRARY)
#  define BARPLUGIN_EXPORT Q_DECL_EXPORT
#else
#  define BARPLUGIN_EXPORT Q_DECL_IMPORT
#endif
