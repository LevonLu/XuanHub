# benchmark_memory.py — 在隔离便携目录按固定场景采样 Windows 进程树内存
"""仅用于本机对照；运行目录须由调用者准备，不读取或修改正式用户配置。"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import hashlib
import re
from pathlib import Path
import subprocess
import time


class MemoryCounters(C.Structure):
    _fields_ = [("cb", W.DWORD), ("PageFaultCount", W.DWORD)] + [
        (name, C.c_size_t) for name in (
            "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
            "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage",
            "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


class ProcessEntry(C.Structure):
    _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD), ("th32ProcessID", W.DWORD),
                ("th32DefaultHeapID", C.c_size_t), ("th32ModuleID", W.DWORD),
                ("cntThreads", W.DWORD), ("th32ParentProcessID", W.DWORD),
                ("pcPriClassBase", W.LONG), ("dwFlags", W.DWORD), ("szExeFile", W.WCHAR * 260)]


kernel = C.WinDLL("kernel32", use_last_error=True)
psapi = C.WinDLL("psapi", use_last_error=True)
kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
kernel.OpenProcess.restype = W.HANDLE
kernel.CloseHandle.argtypes = [W.HANDLE]
kernel.GetExitCodeProcess.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
kernel.CreateToolhelp32Snapshot.argtypes = [W.DWORD, W.DWORD]
kernel.CreateToolhelp32Snapshot.restype = W.HANDLE
kernel.Process32FirstW.argtypes = [W.HANDLE, C.POINTER(ProcessEntry)]
kernel.Process32NextW.argtypes = [W.HANDLE, C.POINTER(ProcessEntry)]
psapi.GetProcessMemoryInfo.argtypes = [W.HANDLE, C.POINTER(MemoryCounters), W.DWORD]
psapi.QueryWorkingSet.argtypes = [W.HANDLE, C.c_void_p, W.DWORD]


def process_tree(root):
    snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
    if snapshot == C.c_void_p(-1).value:
        raise C.WinError(C.get_last_error())
    rows = []
    try:
        entry = ProcessEntry()
        entry.dwSize = C.sizeof(entry)
        ok = kernel.Process32FirstW(snapshot, C.byref(entry))
        while ok:
            rows.append((entry.th32ProcessID, entry.th32ParentProcessID, entry.szExeFile))
            ok = kernel.Process32NextW(snapshot, C.byref(entry))
    finally:
        kernel.CloseHandle(snapshot)
    ids = {root}
    while True:
        expanded = ids | {pid for pid, parent, _ in rows if parent in ids}
        if expanded == ids:
            break
        ids = expanded
    return [(pid, name) for pid, _, name in rows if pid in ids]


def memory(pid):
    handle = kernel.OpenProcess(0x0400 | 0x0010, False, pid)
    if not handle:
        return None
    try:
        counters = MemoryCounters()
        counters.cb = C.sizeof(counters)
        if not psapi.GetProcessMemoryInfo(handle, C.byref(counters), C.sizeof(counters)):
            exit_code = W.DWORD()
            if kernel.GetExitCodeProcess(handle, C.byref(exit_code)) and exit_code.value != 259:
                return None
            raise C.WinError(C.get_last_error())
        # Shared 标志为第 8 位；逐页只累加私有驻留，避免汇总 DLL 共享页面时重复计数。
        pages = max(4096, counters.WorkingSetSize // 4096 + 4096)
        private_ws = None
        for _ in range(3):
            buffer = (C.c_size_t * (pages + 1))()
            if psapi.QueryWorkingSet(handle, buffer, C.sizeof(buffer)):
                private_ws = sum(not (buffer[i + 1] & 0x100) for i in range(buffer[0])) * 4096
                break
            pages *= 2
        if private_ws is None:
            exit_code = W.DWORD()
            if kernel.GetExitCodeProcess(handle, C.byref(exit_code)) and exit_code.value != 259:
                return None
            raise C.WinError(C.get_last_error())
        return {"workingSet": counters.WorkingSetSize, "privateWorkingSet": private_ws,
                "privateCommit": counters.PrivateUsage}
    finally:
        kernel.CloseHandle(handle)


def run(runtime, scenario, seconds):
    # UI 回放走宿主既有 QTest 钩子，不使用坐标猜测，也不触发配置保存。
    commands = ["wait:"] * 20 + ["fc:插件"] + ["wait:"] * 4
    commands += ["fc:Token Usage"] + ["wait:"] * 8 + ["fc:通用", "fc:外观"]
    commands += ["wait:"] * 4 + ["fc:取消"]
    if scenario == "reopen":
        commands = (["fc:外观", "fc:插件", "fc:通用", "fc:取消", "wait:", "wait:"] * 10)
    (runtime / "uitest.txt").write_text("\n".join(commands), encoding="utf-8")
    args = [str(runtime / "XuanHub.exe")]
    if scenario == "settings":
        args += ["--settings", "--uitest"]
    elif scenario == "reopen":
        args += ["--uitest"]
    process = subprocess.Popen(args, cwd=runtime, creationflags=subprocess.CREATE_NO_WINDOW)
    samples = []
    try:
        started = time.monotonic()
        while time.monotonic() - started < seconds:
            if process.poll() is not None:
                raise RuntimeError(f"宿主提前退出: {process.returncode}")
            elapsed = time.monotonic() - started
            rows = []
            for pid, name in process_tree(process.pid):
                stats = memory(pid)
                if stats:
                    rows.append({"pid": pid, "name": name, **stats})
            if not any(row["pid"] == process.pid for row in rows):
                raise RuntimeError("无法读取宿主内存，样本无效")
            samples.append({"seconds": round(elapsed, 2), "processes": rows})
            time.sleep(1)
    finally:
        # 仅结束本次创建的测试宿主；taskbar 模式不保留 AppBar 工作区，worker 在 EOF 自行清理。
        process.terminate()
        process.wait(timeout=10)
    log = (runtime / "xuanhub.log").read_text(encoding="utf-8", errors="replace")
    if scenario != "bar" and ("[uitest] fc miss:" in log or "[uitest] fc: not found" in log):
        raise RuntimeError("回放未命中控件，请检查隔离目录日志")
    if re.search(r"Binding loop|ReferenceError|TypeError|QML error|Cannot assign", log):
        raise RuntimeError("运行日志存在 QML 错误，结果不能作为有效对照")
    if scenario == "settings" and any(f'[uitest] fc "{text}"' not in log
                                      for text in ("插件", "Token Usage", "通用", "外观", "取消")):
        raise RuntimeError("设置回放没有完整执行，结果不能作为有效对照")
    if scenario == "reopen" and (log.count("[MemoryCycle] open") != 10
                                 or log.count('[uitest] fc "取消"') != 10):
        raise RuntimeError("设置重复开关没有完整执行十轮，请检查隔离目录日志")
    return {"scenario": scenario, "samples": samples}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--reopen-only", action="store_true", help="使用隔离 hello fixture 连续开关设置十轮")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    runtime = args.runtime.resolve()
    config = json.loads((runtime / "config.json").read_text(encoding="utf-8-sig"))
    if not all((runtime / name).is_file() for name in ("portable", ".memory-benchmark")) \
            or config.get("bar", {}).get("mode") != "taskbar":
        raise SystemExit("只接受自行准备的便携 taskbar 测试目录")
    if not 1 <= args.repeat <= 20:
        raise SystemExit("repeat 必须为 1–20")
    results = []
    backups = {}
    try:
        if args.reopen_only:
            qml = runtime / "plugins_py/hello/Hello.qml"
            backups = {p: p.read_bytes() for p in (runtime / "config.json", qml)}
            if not any(p.get("id") == "py_hello" for p in config["plugins"]):
                config["plugins"].append({"id": "py_hello", "position": "right", "config": {}})
            (runtime / "config.json").write_text(json.dumps(config, ensure_ascii=False), encoding="utf-8")
            qml.write_text('''// Hello.qml — 仅在隔离目录使用的设置窗口生命周期探针
import QtQuick
Item {
    id: root
    property var plugin
    property var pluginContext
    property int cycles: 0
    width: 1; height: 40
    Timer {
        interval: 3000; repeat: true
        running: root.pluginContext !== null && root.pluginContext !== undefined && root.cycles < 10
        onTriggered: {
            root.cycles++
            console.log("[MemoryCycle] open", root.cycles)
            root.pluginContext.openSettings()
        }
    }
}
''', encoding="utf-8")
        scenarios = (("reopen", 38),) if args.reopen_only else (("bar", 15), ("settings", 30))
        for repeat in range(args.repeat):
            for scenario, seconds in scenarios:
                result = run(runtime, scenario, seconds)
                result["repeat"] = repeat + 1
                result["executableSha256"] = hashlib.sha256((runtime / "XuanHub.exe").read_bytes()).hexdigest()
                result["configSha256"] = hashlib.sha256((runtime / "config.json").read_bytes()).hexdigest()
                results.append(result)
                args.output.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
                (args.output.parent / f"{args.output.stem}-{repeat + 1}-{scenario}.log").write_bytes(
                    (runtime / "xuanhub.log").read_bytes())
                print(f"完成 {repeat + 1}/{args.repeat}: {scenario}", flush=True)
    finally:
        for path, data in backups.items():
            path.write_bytes(data)


if __name__ == "__main__":
    main()
