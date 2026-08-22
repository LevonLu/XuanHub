#!/usr/bin/env python3
"""bridge_worker.py — 单个 Python 子插件的隔离执行进程。

只通过受随机令牌认证的 loopback TCP 与 pybridge 通信；标准输出不承载协议，
诊断统一写 stderr。宿主断开时 finally 会尽力调用插件 stop()，随后进程退出。
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
import socket
import struct
import sys
import traceback
import types
from pathlib import Path
from typing import Any


MAX_MESSAGE_BYTES = 4 * 1024 * 1024
MAX_DEPTH = 32
MAX_SAFE_INTEGER = (1 << 53) - 1  # Qt JSON 数字落到 double；超过此范围转字符串保真。


class HostServices:
    """由宿主绑定插件身份的同步服务代理；插件不能指定或覆盖 plugin id。"""

    def __init__(self, sock: socket.socket) -> None:
        self._sock = sock
        self._next_id = 1

    def _call(self, service: str, method: str, args: dict[str, Any]) -> dict[str, Any]:
        request_id = "host-" + str(self._next_id)
        self._next_id += 1
        _send(self._sock, {
            "type": "host_service",
            "id": request_id,
            "service": service,
            "method": method,
            "args": _json_value(args),
        })
        response = _recv(self._sock)
        if response.get("type") != "host_service_response" or response.get("id") != request_id:
            raise RuntimeError("host service response mismatch")
        return {
            "success": bool(response.get("ok", False)),
            "value": response.get("result"),
            "errorCode": str(response.get("errorCode", "")),
            "errorMessage": str(response.get("error", "")),
        }

    def log(self, level: str, message: str, fields: dict[str, Any] | None = None) -> dict[str, Any]:
        return self._call("logger", "log", {
            "level": str(level), "message": str(message), "fields": fields or {},
        })

    def notify(self, title: str, message: str) -> dict[str, Any]:
        return self._call("notifications", "show", {
            "title": str(title), "message": str(message),
        })


def _install_host_module(services: HostServices) -> None:
    module = types.ModuleType("xuanhub_host")
    module.services = services
    module.log = services.log
    module.notify = services.notify
    sys.modules["xuanhub_host"] = module


def _log(message: str) -> None:
    print("[PyWorker] " + message, file=sys.stderr, flush=True)


def _json_value(value: Any, depth: int = 0) -> Any:
    """把插件结果限制在 JSON/QML 安全交集，避免对象图或 NaN 撑爆 IPC。"""
    if depth > MAX_DEPTH:
        return None
    if value is None or isinstance(value, (bool, str)):
        return value
    if isinstance(value, int):
        return value if -MAX_SAFE_INTEGER <= value <= MAX_SAFE_INTEGER else str(value)
    if isinstance(value, float):
        return value if math.isfinite(value) else str(value)
    if isinstance(value, dict):
        return {str(key): _json_value(item, depth + 1) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_json_value(item, depth + 1) for item in value]
    return str(value)


def _send(sock: socket.socket, message: dict[str, Any]) -> None:
    payload = json.dumps(message, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode("utf-8")
    if len(payload) > MAX_MESSAGE_BYTES:
        raise ValueError("response exceeds IPC message limit")
    sock.sendall(struct.pack("!I", len(payload)) + payload)


def _recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            raise EOFError("host closed IPC connection")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def _recv(sock: socket.socket) -> dict[str, Any]:
    size = struct.unpack("!I", _recv_exact(sock, 4))[0]
    if size > MAX_MESSAGE_BYTES:
        raise ValueError("request exceeds IPC message limit")
    message = json.loads(_recv_exact(sock, size).decode("utf-8"))
    if not isinstance(message, dict):
        raise ValueError("request must be a JSON object")
    return message


def _load_plugin(plugin_dir: Path, plugin_id: str, config: dict[str, Any], locale: str,
                 host_services: HostServices) -> Any:
    main_file = plugin_dir / "main.py"
    if not main_file.is_file():
        raise RuntimeError("main.py is missing")
    module_name = "xuanhub_worker_" + "".join(
        char if char.isalnum() or char == "_" else "_" for char in plugin_id
    )
    if str(plugin_dir) not in sys.path:
        # 保持旧桥接的末尾追加语义：不让同名辅助模块覆盖标准库或 site-packages。
        sys.path.append(str(plugin_dir))
    spec = importlib.util.spec_from_file_location(module_name, main_file)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot create import specification")
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    _install_host_module(host_services)
    spec.loader.exec_module(module)
    if hasattr(module, "set_host_services"):
        module.set_host_services(host_services)
    if hasattr(module, "init"):
        module.init(config)
    if hasattr(module, "set_locale"):
        module.set_locale(locale)
    if not hasattr(module, "update"):
        raise RuntimeError("main.py must define update()")
    return module


def _stop_plugin(module: Any) -> None:
    if module is not None and hasattr(module, "stop"):
        try:
            module.stop()
        except Exception:
            _log("stop() failed:\n" + traceback.format_exc(limit=8))


def _error_text() -> str:
    return traceback.format_exc(limit=12)[-6000:]


def run(port: int, token: str, plugin_dir: Path, plugin_id: str) -> int:
    module = None
    stop_called = False
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
            sock.settimeout(None)
            host_services = HostServices(sock)
            _send(sock, {"type": "hello", "token": token})
            while True:
                request = _recv(sock)
                request_id = str(request.get("id", ""))
                if request.get("type") != "request" or not request_id:
                    raise ValueError("malformed request")
                op = request.get("op")
                data = request.get("data")
                if not isinstance(data, dict):
                    data = {}
                try:
                    if op == "init":
                        if module is not None:
                            raise RuntimeError("worker was initialized twice")
                        config = data.get("config", {})
                        if not isinstance(config, dict):
                            raise ValueError("init config must be an object")
                        locale = data.get("locale", "en-US")
                        if not isinstance(locale, str):
                            raise ValueError("init locale must be a string")
                        module = _load_plugin(plugin_dir, plugin_id, config, locale, host_services)
                        capabilities = []
                        if hasattr(module, "set_config"):
                            capabilities.append("set_config")
                        capabilities.append("host_services.v1")
                        result: Any = {"ready": True, "capabilities": capabilities}
                    elif module is None:
                        raise RuntimeError("worker is not initialized")
                    elif op == "update":
                        result = module.update()
                        if not isinstance(result, dict):
                            raise TypeError("update() must return a dict")
                    elif op == "invoke":
                        name = data.get("name")
                        args = data.get("args", [])
                        if not isinstance(name, str) or not isinstance(args, list):
                            raise ValueError("invoke requires string name and list args")
                        result = module.invoke(name, args) if hasattr(module, "invoke") else None
                    elif op == "set_locale":
                        locale = data.get("locale", "en-US")
                        if not isinstance(locale, str):
                            raise ValueError("locale must be a string")
                        if hasattr(module, "set_locale"):
                            module.set_locale(locale)
                        result = None
                    elif op == "set_config":
                        config = data.get("config", {})
                        if not isinstance(config, dict):
                            raise ValueError("set_config config must be an object")
                        if not hasattr(module, "set_config"):
                            raise RuntimeError("plugin does not support set_config")
                        module.set_config(config)
                        result = None
                    elif op == "stop":
                        _stop_plugin(module)
                        stop_called = True
                        _send(sock, {"type": "response", "id": request_id, "ok": True, "result": None})
                        return 0
                    else:
                        raise ValueError("unknown operation: " + str(op))
                    _send(sock, {"type": "response", "id": request_id, "ok": True,
                                 "result": _json_value(result)})
                except Exception:
                    _send(sock, {"type": "response", "id": request_id, "ok": False,
                                 "error": _error_text()})
    except (ConnectionError, EOFError, OSError, ValueError) as exc:
        _log(str(exc))
        return 2
    finally:
        if not stop_called:
            _stop_plugin(module)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--token", required=True)
    parser.add_argument("--plugin-dir", required=True, type=Path)
    parser.add_argument("--plugin-id", required=True)
    args = parser.parse_args()
    return run(args.port, args.token, args.plugin_dir.resolve(), args.plugin_id)


if __name__ == "__main__":
    raise SystemExit(main())
