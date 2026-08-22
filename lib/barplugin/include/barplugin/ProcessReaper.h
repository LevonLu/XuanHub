// ProcessReaper.h — 跨插件 DLL 安全回收外部子进程
#pragma once

#include "barplugin/barplugin_export.h"

class QProcess;

namespace xuanhub {

// 将 process 的所有权移交给常驻 barplugin.dll。调用方须先断开位于自身 DLL
// 的接收回调；reaper 在 graceMs 后终止、再兜底 kill。
BARPLUGIN_EXPORT void reapProcess(QProcess* process, int graceMs = 0);
// 插件宿主 DLL 卸载前同步清空已移交进程，确保 QProcess 析构时原 DLL 的
// 信号/事件代码仍然有效。正常运行期间仍使用 reapProcess 的异步宽限期。
BARPLUGIN_EXPORT void drainReapedProcesses(int waitMs = 1000);

} // namespace xuanhub
