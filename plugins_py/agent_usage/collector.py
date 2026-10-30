# collector.py — 独立 Python 采集进程，只输出有限的统计结果和稳定错误码
from __future__ import annotations

import json
from pathlib import Path
import sys

# -I 禁用环境注入与当前目录搜索；只显式加入插件自己的模块目录。
sys.path.insert(0, str(Path(__file__).resolve().parent))


def run(request):
    from native.engine import VERSION, collect
    lane = request.get("lane")
    if lane not in {"usage", "quota", "detail"} or not isinstance(request.get("cacheDir"), str):
        raise ValueError("invalid_request")
    if lane != "detail":
        from native.network import configure_proxy
        configure_proxy(request.get("proxyMode", "auto"), request.get("proxyUrl", ""), cache_dir=Path(request["cacheDir"]) / "native-v2")
    if lane == "quota":
        from native.quota import Quotas
        return {"providers": Quotas(cache_dir=Path(request["cacheDir"]) / "native-v2").collect(bool(request.get("devinEnabled"))), "version": VERSION}
    if not all(isinstance(request.get(k), (int, float)) for k in ("fromMs", "toMs")):
        raise ValueError("invalid_request")
    if not 0 <= request["fromMs"] < request["toMs"] <= 253_402_300_800_000:
        raise ValueError("invalid_request")
    return collect(request)


if __name__ == "__main__":
    try:
        raw = sys.stdin.buffer.read(65537)
        if len(raw) > 65536:
            raise ValueError("invalid_request")
        result = {"ok": True, "data": run(json.loads(raw))}
    except Exception as error:
        allowed = {"data_not_ready", "invalid_request", "compression_unavailable", "crypto_unavailable", "proxy_failed"}
        result = {"ok": False, "error": str(error) if str(error) in allowed else "collector_failed"}
    payload = json.dumps(result, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
    sys.stdout.buffer.write(payload if len(payload) <= 2 * 1024 * 1024 else b'{"ok":false,"error":"response_too_large"}')
