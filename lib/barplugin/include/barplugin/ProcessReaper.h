// ProcessReaper.h — 跨插件 DLL 安全回收外部子进程
#pragma once

#include "barplugin/barplugin_export.h"

class QProcess;

namespace xuanhub {

// 将 process 的所有权移交给常驻 barplugin.dll。调用方可立即销毁/卸载其插件 DLL；
// reaper 在 graceMs 后终止、再兜底 kill，且绝不在调用方 DLL 中保留异步回调。
BARPLUGIN_EXPORT void reapProcess(QProcess* process, int graceMs = 0);

} // namespace xuanhub
