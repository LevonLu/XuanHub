# environment_installer.py — 经用户确认后创建私有环境并仅从离线 wheel 安装依赖
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import venv
from typing import Any


def install(payload: dict[str, Any]) -> dict[str, Any]:
    environment = Path(str(payload["environment"]))
    wheels = Path(str(payload["wheels"]))
    requirements = payload.get("requirements", [])
    if not isinstance(requirements, list) or not all(
            isinstance(item, str) and item for item in requirements):
        raise ValueError("requirements must be non-empty strings")
    if not wheels.is_dir() or not any(wheels.glob("*.whl")):
        raise ValueError("offline wheel directory is missing or empty")

    private_python = environment / "Scripts" / "python.exe"
    if not private_python.is_file():
        environment.parent.mkdir(parents=True, exist_ok=True)
        # 不清理已有目录；损坏环境由用户删除后再次确认，避免隐式破坏数据。
        venv.EnvBuilder(with_pip=True, clear=False, upgrade=False).create(environment)
    if not private_python.is_file():
        raise RuntimeError("private environment did not create python.exe")

    command = [
        str(private_python), "-m", "pip", "install",
        "--disable-pip-version-check", "--no-index", "--only-binary=:all:",
        "--find-links", str(wheels), *requirements,
    ]
    completed = subprocess.run(command, capture_output=True, text=True,
                               encoding="utf-8", errors="replace", timeout=300)
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout).strip().splitlines()
        raise RuntimeError(detail[-1] if detail else "offline pip install failed")
    return {"ok": True, "python": str(private_python)}


def main() -> int:
    try:
        payload = json.load(sys.stdin)
        if not isinstance(payload, dict):
            raise TypeError("installer input must be an object")
        result = install(payload)
        json.dump(result, sys.stdout, ensure_ascii=True, separators=(",", ":"))
        return 0
    except Exception as error:
        json.dump({"ok": False, "error": str(error)}, sys.stdout,
                  ensure_ascii=True, separators=(",", ":"))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
