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
        from native.accounts import Accounts
        from native.network import enable_request_coalescing
        enable_request_coalescing()
        import threading
        cache = Path(request["cacheDir"]) / "native-v2"
        force = request.get("forceAccounts", [])
        partial = bool(force and "*" not in force)
        selected = None if not partial else {key.removeprefix("auto:") for key in force if key.startswith("auto:")}
        aliases = {"codex-spark": "codex", "opencode-go": "opencodeGo", "command-code": "commandCode", "qoder-cn": "qoderCn", "coding-plan": "codingPlan", "agent-plan": "agentPlan"}
        if selected is not None:
            selected = {aliases.get(key, key) for key in selected}
        accounts, errors = [], []
        def collect_accounts():
            try:
                configured = request.get("accounts", [])
                if partial:
                    configured = [a for a in configured if a["id"] in force]
                accounts.extend(Accounts(cache).collect(configured, force, request.get("quotaEnabled", True)))
            except Exception:
                errors.append("accounts_unavailable")
        worker = threading.Thread(target=collect_accounts, daemon=True)
        worker.start()
        providers = Quotas(cache_dir=cache).collect(bool(request.get("devinEnabled")), selected) if request.get("quotaEnabled", True) else {}
        worker.join(timeout=33)
        return {"providers": providers, "accounts": accounts, "partial": partial, "accountError": bool(errors or worker.is_alive()), "version": VERSION}
    if not all(isinstance(request.get(k), (int, float)) for k in ("fromMs", "toMs")):
        raise ValueError("invalid_request")
    if not 0 <= request["fromMs"] < request["toMs"] <= 253_402_300_800_000:
        raise ValueError("invalid_request")
    return collect(request)


if __name__ == "__main__":
    try:
        raw = sys.stdin.buffer.read(2 * 1024 * 1024 + 1)
        if len(raw) > 2 * 1024 * 1024:
            raise ValueError("invalid_request")
        result = {"ok": True, "data": run(json.loads(raw))}
    except Exception as error:
        allowed = {"data_not_ready", "invalid_request", "compression_unavailable", "crypto_unavailable", "proxy_failed"}
        result = {"ok": False, "error": str(error) if str(error) in allowed else "collector_failed"}
    payload = json.dumps(result, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
    sys.stdout.buffer.write(payload if len(payload) <= 2 * 1024 * 1024 else b'{"ok":false,"error":"response_too_large"}')
