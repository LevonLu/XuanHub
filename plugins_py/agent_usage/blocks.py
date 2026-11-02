# blocks.py — 将列配置解析为不含凭据的显示块与账户快照
from __future__ import annotations

from model import NAMES, BAR_QUOTA_NAMES


def account_views(providers, manual):
    values = [{**q, "id": "auto:" + key, "providerId": key, "name": NAMES.get(key, key), "automatic": True}
              for key, q in providers.items() if q.get("configured") or q.get("status") == "reauth"]
    codex = providers.get("codex")
    if codex and (codex.get("configured") or codex.get("status") == "reauth"):
        values.append({**codex, "id": "auto:codex-spark", "name": "Codex Spark", "providerId": "codex", "automatic": True,
                       "windows": [w for w in codex.get("windows", []) if w["id"].startswith("spark_")]})
        for value in values:
            if value["id"] == "auto:codex":
                value["windows"] = [w for w in value.get("windows", []) if not w["id"].startswith("spark_")]
    return values + list(manual)


def resolve_columns(columns, sources, accounts, total, usage_ready):
    by_account = {a["id"]: a for a in accounts}

    def resolve(spec):
        metric, target = spec.get("metric", "empty"), spec.get("targetId", "")
        value = {**spec, "metric": metric, "name": "", "value": None, "stale": False}
        if metric in {"empty", "tokens"}:
            if metric == "tokens":
                stats = sources.get(target)
                if target in {"pi", "prime-agent"}:
                    stats = {"total_tokens": sum(s.get("total_tokens", 0) for key, s in sources.items() if key == target or key.startswith(target + "-"))}
                value.update(name=NAMES.get(target, target) if target != "total" else "", value=(total if target == "total" else (stats or {}).get("total_tokens")) if usage_ready else None)
            return value
        account = by_account.get(target, {})
        value.update(name=account.get("name") or BAR_QUOTA_NAMES.get(target.removeprefix("auto:"), target), stale=account.get("stale", False), status=account.get("status", "not_configured"))
        if metric in {"balance", "spendToday"}:
            balances = account.get("balances", [])
            currency = spec.get("currency", "")
            balance = next((b for b in balances if b["currency"] == currency), {}) if currency else next(iter(balances), {})
            value.update(value=balance.get("balance" if metric == "balance" else "spendToday"), currency=balance.get("currency", currency), affected=metric == "spendToday" and balance.get("affected", False))
        else:
            windows = account.get("windows", [])
            seconds = {"quota5h": 18000, "quota7d": 604800}.get(metric)
            wanted = spec.get("windowId", "primary_window")
            found = next((w for w in windows if w.get("seconds") == seconds and not any(k in w["id"] for k in ("opus", "sonnet"))), {}) if seconds else next((w for w in windows if w["id"] == wanted), {})
            value.update(value=found.get("used"), resetAt=found.get("resetAt", ""), windowLabel=found.get("label", wanted))
        return value

    return [{**col, **{slot: resolve(col.get(slot, {})) for slot in ("single", "top", "bottom")}} for col in columns]
