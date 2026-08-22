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
PROTOCOL_VERSION = 2
PROTOCOL_CAPABILITIES = [
    "requestResponseV1", "callResults", "callProgress", "cancelQueued",
    "hostServicesV1", "eventBusV2",
]


class CallError(Exception):
    """插件可显式抛出的业务错误；code 与 details 会稳定返回给 QML。"""

    def __init__(self, code: str, message: str,
                 details: dict[str, Any] | None = None) -> None:
        super().__init__(str(message))
        normalized = str(code)
        if not normalized or len(normalized) > 64 \
                or any(not (char.isalnum() or char in "_.-") for char in normalized):
            normalized = "plugin_error"
        self.code = normalized
        self.details = details if isinstance(details, dict) else {}


class CallRuntime:
    """只在 invoke 主线程有效的进度发送器，内部请求 ID 不暴露给插件。"""

    def __init__(self, sock: socket.socket) -> None:
        self._sock = sock
        self._request_id = ""

    def begin(self, request_id: str) -> None:
        self._request_id = request_id

    def end(self) -> None:
        self._request_id = ""

    def progress(self, value: float,
                 details: dict[str, Any] | None = None) -> None:
        if not self._request_id:
            raise RuntimeError("report_progress() is only valid during invoke()")
        if isinstance(value, bool) or not isinstance(value, (int, float)) \
                or not math.isfinite(float(value)) or not 0.0 <= float(value) <= 1.0:
            raise ValueError("progress must be a finite number from 0 to 1")
        if details is not None and not isinstance(details, dict):
            raise TypeError("progress details must be a dict")
        _send(self._sock, {
            "type": "call_progress",
            "id": self._request_id,
            "progress": float(value),
            "details": _json_value(details or {}),
        })


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


class EventBusServices:
    """EventBus V2 代理；身份和 Topic 权限由宿主绑定，handler 只在主循环串行调用。"""

    def __init__(self, sock: socket.socket) -> None:
        self._sock = sock
        self._next_id = 1
        self._handlers: dict[str, list[Any]] = {}

    def _call(self, operation: str, args: dict[str, Any]) -> dict[str, Any]:
        request_id = "event-" + str(self._next_id)
        self._next_id += 1
        _send(self._sock, {
            "type": "event_bus",
            "id": request_id,
            "operation": operation,
            "args": _json_value(args),
        })
        response = _recv(self._sock)
        if response.get("type") != "event_bus_response" or response.get("id") != request_id:
            raise RuntimeError("EventBus response mismatch")
        return {
            "success": bool(response.get("ok", False)),
            "value": response.get("result"),
            "errorCode": str(response.get("errorCode", "")),
            "errorMessage": str(response.get("error", "")),
        }

    def publish(self, topic: str, data: dict[str, Any] | None = None) -> dict[str, Any]:
        return self._call("publish", {"topic": str(topic), "data": data or {}})

    def subscribe(self, topic: str, handler: Any,
                  replay_retained: bool = False) -> dict[str, Any]:
        if not callable(handler):
            raise TypeError("EventBus handler must be callable")
        topic = str(topic)
        handlers = self._handlers.setdefault(topic, [])
        if handler not in handlers:
            handlers.append(handler)
        result = self._call("subscribe", {
            "topic": topic, "replayRetained": bool(replay_retained),
        })
        if not result["success"] and handler in handlers:
            handlers.remove(handler)
        return result

    def request(self, topic: str, data: dict[str, Any] | None = None,
                timeout_ms: int = 5000) -> dict[str, Any]:
        result = self._call("request", {
            "topic": str(topic), "data": data or {}, "timeoutMs": int(timeout_ms),
        })
        return result["value"] if result["success"] else result

    def respond(self, request_id: str, data: dict[str, Any] | None = None,
                error_code: str = "", error_message: str = "") -> dict[str, Any]:
        return self._call("respond", {
            "requestId": str(request_id), "data": data or {},
            "errorCode": str(error_code), "errorMessage": str(error_message),
        })

    def state(self, topic: str) -> dict[str, Any]:
        result = self._call("state", {"topic": str(topic)})
        return result["value"] if result["success"] else result

    def _deliver(self, topic: str, envelope: dict[str, Any]) -> None:
        for handler in list(self._handlers.get(topic, [])):
            try:
                handler(envelope)
            except Exception:
                _log("EventBus handler failed:\n" + traceback.format_exc(limit=8))


def _install_host_module(services: HostServices, events: EventBusServices,
                         calls: CallRuntime) -> None:
    module = types.ModuleType("xuanhub_host")
    module.services = services
    module.log = services.log
    module.notify = services.notify
    module.events = events
    module.CallError = CallError
    module.report_progress = calls.progress
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


def _import_plugin(plugin_dir: Path, plugin_id: str,
                   host_services: HostServices, event_bus: EventBusServices,
                   calls: CallRuntime) -> Any:
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
    _install_host_module(host_services, event_bus, calls)
    spec.loader.exec_module(module)
    if hasattr(module, "set_host_services"):
        module.set_host_services(host_services)
    if hasattr(module, "set_event_bus"):
        module.set_event_bus(event_bus)
    return module


def _validation_result(module: Any, config: dict[str, Any]) -> dict[str, Any]:
    validator = getattr(module, "validate_config", None)
    if not callable(validator):
        return {"supported": False, "valid": True, "error": ""}
    verdict = validator(dict(config))
    if isinstance(verdict, bool):
        return {
            "supported": True,
            "valid": verdict,
            "error": "" if verdict else "validate_config() rejected the configuration",
        }
    if isinstance(verdict, dict):
        valid = bool(verdict.get("ok", False))
        errors = verdict.get("errors", [])
        if isinstance(errors, list):
            detail = "; ".join(str(item) for item in errors)
        else:
            detail = str(errors)
        return {
            "supported": True,
            "valid": valid,
            "error": "" if valid else (detail or "validate_config() rejected the configuration"),
        }
    raise TypeError("validate_config() must return bool or {ok, errors}")


def _prepare_config(module: Any, current_version: int, target_version: int,
                    config: dict[str, Any]) -> dict[str, Any]:
    declared_version = getattr(module, "CONFIG_VERSION", target_version)
    if isinstance(declared_version, bool) or not isinstance(declared_version, int) \
            or declared_version < 1:
        raise ValueError("CONFIG_VERSION must be a positive integer")
    if declared_version != target_version:
        raise ValueError(
            f"CONFIG_VERSION {declared_version} does not match manifest {target_version}")
    if current_version > target_version:
        raise ValueError(
            f"config version {current_version} is newer than plugin target {target_version}")

    migrated = dict(config)
    if current_version < target_version:
        migrator = getattr(module, "migrate_config", None)
        if not callable(migrator):
            raise RuntimeError("plugin does not provide migrate_config()")
        migrated = migrator(current_version, dict(config))
        if not isinstance(migrated, dict):
            raise TypeError("migrate_config() must return a dict")

    validation = _validation_result(module, migrated)
    if not validation["valid"]:
        raise ValueError(validation["error"])
    return migrated


def _initialize_plugin(module: Any, config: dict[str, Any], locale: str) -> None:
    if hasattr(module, "init"):
        module.init(config)
    if hasattr(module, "set_locale"):
        module.set_locale(locale)
    if not hasattr(module, "update"):
        raise RuntimeError("main.py must define update()")


def _stop_plugin(module: Any) -> None:
    if module is not None and hasattr(module, "stop"):
        try:
            module.stop()
        except Exception:
            _log("stop() failed:\n" + traceback.format_exc(limit=8))


def _error_text() -> str:
    return traceback.format_exc(limit=12)[-6000:]


def _exception_error_code(error: Exception) -> str:
    if isinstance(error, NotImplementedError):
        return "unsupported_operation"
    if isinstance(error, TimeoutError):
        return "timeout"
    if isinstance(error, ValueError):
        return "invalid_argument"
    return "plugin_exception"


def run(port: int, token: str, plugin_dir: Path, plugin_id: str) -> int:
    module = None
    stop_called = False
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
            sock.settimeout(None)
            host_services = HostServices(sock)
            event_bus = EventBusServices(sock)
            calls = CallRuntime(sock)
            _send(sock, {
                "type": "hello",
                "token": token,
                "protocol": PROTOCOL_VERSION,
                "pluginId": plugin_id,
                "capabilities": PROTOCOL_CAPABILITIES,
            })
            while True:
                request = _recv(sock)
                if request.get("type") == "event_bus_event":
                    delivery_id = str(request.get("id", ""))
                    try:
                        topic = str(request.get("topic", ""))
                        envelope = request.get("envelope", {})
                        if not isinstance(envelope, dict):
                            envelope = {}
                        event_bus._deliver(topic, envelope)
                    finally:
                        _send(sock, {"type": "event_bus_event_ack", "id": delivery_id})
                    continue
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
                        current_version = data.get("configVersion", 1)
                        target_version = data.get("targetConfigVersion", 1)
                        if isinstance(current_version, bool) or not isinstance(current_version, int) \
                                or current_version < 1:
                            raise ValueError("init configVersion must be a positive integer")
                        if isinstance(target_version, bool) or not isinstance(target_version, int) \
                                or target_version < 1:
                            raise ValueError("init targetConfigVersion must be a positive integer")
                        module = _import_plugin(plugin_dir, plugin_id,
                                                host_services, event_bus, calls)
                        prepared_config = _prepare_config(
                            module, current_version, target_version, config)
                        _initialize_plugin(module, prepared_config, locale)
                        capabilities = []
                        if hasattr(module, "set_config"):
                            capabilities.append("set_config")
                        if callable(getattr(module, "validate_config", None)):
                            capabilities.append("validate_config")
                        capabilities.append("host_services.v1")
                        capabilities.append("event_bus.v2")
                        capabilities.append("config_migration.v1")
                        result: Any = {
                            "ready": True,
                            "capabilities": capabilities,
                            "migration": {
                                "required": current_version < target_version,
                                "config": prepared_config,
                            },
                        }
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
                        calls.begin(request_id)
                        try:
                            result = module.invoke(name, args) if hasattr(module, "invoke") else None
                        finally:
                            calls.end()
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
                    elif op == "validate_config":
                        config = data.get("config", {})
                        if not isinstance(config, dict):
                            raise ValueError("validate_config config must be an object")
                        result = _validation_result(module, config)
                    elif op == "stop":
                        _stop_plugin(module)
                        stop_called = True
                        _send(sock, {"type": "response", "id": request_id, "ok": True, "result": None})
                        return 0
                    else:
                        raise ValueError("unknown operation: " + str(op))
                    _send(sock, {"type": "response", "id": request_id, "ok": True,
                                 "result": _json_value(result)})
                except CallError as exc:
                    _send(sock, {"type": "response", "id": request_id, "ok": False,
                                 "errorCode": exc.code, "error": str(exc),
                                 "details": _json_value(exc.details)})
                except Exception as exc:
                    _send(sock, {"type": "response", "id": request_id, "ok": False,
                                 "errorCode": _exception_error_code(exc),
                                 "error": str(exc) or type(exc).__name__,
                                 "details": {
                                     "exceptionType": type(exc).__name__,
                                     "traceback": _error_text(),
                                 }})
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
