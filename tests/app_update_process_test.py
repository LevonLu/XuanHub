# app_update_process_test.py — 真实 Windows 进程覆盖正常更新、文件占用和启动失败回滚
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

KERNEL = ctypes.WinDLL("kernel32", use_last_error=True)
KERNEL.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
KERNEL.OpenProcess.restype = wintypes.HANDLE
KERNEL.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                             wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
KERNEL.CreateFileW.restype = wintypes.HANDLE
KERNEL.CloseHandle.argtypes = [wintypes.HANDLE]
KERNEL.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
KERNEL.WaitForSingleObject.restype = wintypes.DWORD


def wait_until(check, seconds=25):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(0.05)
    raise AssertionError("等待更新进程完成超时")


def write_manifest(root, version, names):
    entries = []
    for name in sorted(names):
        file = root / name
        entries.append({"path": name, "size": file.stat().st_size,
                        "sha256": hashlib.sha256(file.read_bytes()).hexdigest()})
    (root / "update-manifest.json").write_text(json.dumps({
        "schemaVersion": 1, "version": version, "files": entries}), encoding="utf-8")


class AppUpdateProcessTest(unittest.TestCase):
    def scenario(self, *, failure=False, locked=False, interrupted=False):
        fixture = Path(os.environ["XUANHUB_UPDATE_FIXTURE"])
        helper = Path(os.environ["XUANHUB_UPDATE_HELPER"])
        runtime = Path(os.environ["XUANHUB_UPDATE_CORE"])
        with tempfile.TemporaryDirectory(prefix="XuanHubUpdateProcess-") as temporary:
            root = Path(temporary)
            target, job = root / "target", root / "job"
            stage, independent = job / "stage", job / "helper"
            for folder in (target, stage, independent):
                folder.mkdir(parents=True)
            for folder in (target, stage):
                shutil.copy2(fixture, folder / "XuanHub.exe")
                shutil.copy2(helper, folder / "XuanHubUpdater.exe")
                shutil.copy2(runtime, folder / "Qt6Core.dll")
                (folder / "barplugin.dll").write_bytes(b"fixture")
                (folder / "official.txt").write_bytes(b"old" if folder == target else b"new")
            shutil.copy2(helper, independent / "XuanHubUpdater.exe")
            shutil.copy2(runtime, independent / "Qt6Core.dll")
            # 使用与发行更新器相同的 CRT 复制策略，测试不依赖 PATH 中的 Qt。
            for library in helper.parent.glob("*.dll"):
                if library.name.lower().startswith(("vcruntime", "msvcp", "concrt")):
                    for folder in (target, stage, independent):
                        shutil.copy2(library, folder / library.name)
            old_names = [p.name for p in target.iterdir() if p.is_file()]
            new_names = [p.name for p in stage.iterdir() if p.is_file()]
            (target / "obsolete.txt").write_bytes(b"old-only")
            old_names.append("obsolete.txt")
            (stage / "new.txt").write_bytes(b"new-only")
            new_names.append("new.txt")
            write_manifest(target, "0.4.4", old_names)
            write_manifest(stage, "0.4.5", new_names)
            (target / "config.json").write_bytes(b"user-config")
            (target / "secrets.json").write_bytes(b"user-secrets")
            (target / "plugins/custom").mkdir(parents=True)
            (target / "plugins/custom/main.py").write_bytes(b"user-plugin")
            if failure:
                (target / "fail-startup").write_bytes(b"fixture-control")
            data = {"schemaVersion": 1, "target": str(target), "oldVersion": "0.4.4",
                    "version": "0.4.5", "token": "a" * 64}
            (job / "job.json").write_text(json.dumps(data), encoding="utf-8")
            lock = None
            try:
                if locked:
                    lock = KERNEL.CreateFileW(str(target / "official.txt"), 0x80000000,
                                              1, None, 3, 0, None)
                    self.assertNotIn(lock, (None, ctypes.c_void_p(-1).value))
                command = [str(target / "XuanHub.exe"), "--launch-update", str(job)]
                if interrupted:
                    (job / "backup").mkdir()
                    shutil.copy2(target / "official.txt", job / "backup/official.txt")
                    (target / "official.txt").write_bytes(b"new")
                    (job / "journal.json").write_text(json.dumps({"entries": [
                        {"path": "official.txt", "existed": True}]}), encoding="utf-8")
                    (target / ".xuanhub-update.json").write_text(json.dumps({
                        "directory": str(job), "token": "a" * 64}), encoding="utf-8")
                    command = [str(independent / "XuanHubUpdater.exe"), "--recover-offline", str(job)]
                parent = subprocess.run(command,
                                        timeout=20, creationflags=subprocess.CREATE_NO_WINDOW,
                                        capture_output=True)
                self.assertEqual(parent.returncode, 1 if interrupted else 0, parent.stderr)
                wait_until(lambda: (job / "result.json").is_file())
                result = json.loads((job / "result.json").read_text(encoding="utf-8"))
                self.assertEqual(result["status"], "rolled-back" if failure or locked or interrupted else "updated", result)
            finally:
                if lock:
                    KERNEL.CloseHandle(lock)
            if failure or locked or interrupted:
                wait_until(lambda: (target / "old-restarted.json").is_file())
                self.assertEqual((target / "official.txt").read_bytes(), b"old")
                self.assertTrue((target / "obsolete.txt").is_file())
                self.assertFalse((target / "new.txt").exists())
            else:
                self.assertEqual((target / "official.txt").read_bytes(), b"new")
                self.assertFalse((target / "obsolete.txt").exists())
                receipt = json.loads((job / "app-ready.json").read_text(encoding="utf-8"))
                process = KERNEL.OpenProcess(0x100000, False, receipt["pid"])
                if process:
                    try:
                        self.assertEqual(KERNEL.WaitForSingleObject(process, 15000), 0)
                    finally:
                        KERNEL.CloseHandle(process)
            self.assertEqual((target / "config.json").read_bytes(), b"user-config")
            self.assertEqual((target / "secrets.json").read_bytes(), b"user-secrets")
            self.assertEqual((target / "plugins/custom/main.py").read_bytes(), b"user-plugin")
            self.assertFalse((target / ".xuanhub-update.json").exists())
            if failure:
                worker = json.loads((target / "probe-worker.json").read_text(encoding="utf-8"))["pid"]
                process = KERNEL.OpenProcess(0x100000, False, worker)
                if process:
                    try:
                        self.assertEqual(KERNEL.WaitForSingleObject(process, 3000), 0)
                    finally:
                        KERNEL.CloseHandle(process)

    def test_success(self):
        self.scenario()

    def test_failed_startup_restores_old_and_stops_probe_workers(self):
        self.scenario(failure=True)

    def test_locked_file_restores_partial_update(self):
        self.scenario(locked=True)

    def test_offline_recovery_uses_the_persisted_journal(self):
        self.scenario(interrupted=True)


if __name__ == "__main__":
    unittest.main()
