# main.py — 米家生命周期，宿主回调只交接内存结果和密钥
from __future__ import annotations

import xuanhub_host
from cloud import CloudError
from runtime import Runtime

CONFIG_VERSION = 1
_runtime = None


def validate_config(config):
    interval = config.get("refreshSeconds", 10)
    heartbeat = config.get("intervalMs", 1000)
    ok = type(interval) is int and 5 <= interval <= 300 and type(heartbeat) is int and heartbeat == 1000
    return {"ok": ok, "errors": [] if ok else ["invalid refresh interval"]}


def init(config):
    global _runtime
    _runtime = Runtime(xuanhub_host)
    _runtime.init(config)


def set_config(config):
    _runtime.configure(config)


def update():
    return _runtime.update() if _runtime else {"status": "signed_out"}


def invoke(name, args):
    try:
        return _runtime.invoke(name, args)
    except CloudError as error:
        raise xuanhub_host.CallError(error.code, error.code) from None


def stop():
    if _runtime:
        _runtime.engine.close()
