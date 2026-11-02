# accounts.py — 手动账户额度、十进制余额差与按凭据隔离的持久化
from __future__ import annotations

import copy
from datetime import datetime, timedelta
from decimal import Decimal, InvalidOperation
import hashlib
import json
from pathlib import Path
import queue
import sqlite3
import threading
import time
import urllib.parse

from .common import iso, obj, timestamp
from .network import Http, RequestError, bearer
from .quota import result, window
from .quota_products import go_html, go_windows, zcode_quota


def money(value):
    try:
        value = Decimal(str(value))
        if value.is_finite():
            return value
    except InvalidOperation:
        pass
    raise RequestError("invalid_response")


def business_day(now, account):
    boundary = now.replace(hour=account.get("startHour", 0), minute=account.get("startMin", 0), second=0, microsecond=0)
    return (boundary - timedelta(days=1) if now < boundary else boundary).date().isoformat()


def observe(previous, balance, now, account, legacy=None):
    day = business_day(now, account)
    stamp = now.isoformat(timespec="seconds")
    if not previous or previous.get("day") != day:
        # 旧基线只有明确的当日记录才能导入，不能用历史金额伪造完整一天。
        previous = {"day": day, "last": str(balance), "spent": "0", "since": stamp, "affected": False}
        if legacy and legacy.get("identityVerified") is True and legacy.get("start_date") == day and legacy.get("current_balance") is not None and legacy.get("start_time"):
            old = money(legacy["current_balance"])
            start = money(legacy.get("start_balance", old))
            previous.update(last=str(old), spent=str(max(Decimal(0), start - old)), since=day + "T" + legacy["start_time"] + ":00")
    delta = money(previous["last"]) - balance
    return {**previous, "last": str(balance), "spent": str(money(previous["spent"]) + max(Decimal(0), delta)),
            "affected": bool(previous.get("affected") or delta < 0), "observedAt": stamp}


def account_key(account):
    # 同一凭据和资源共用一次请求；指纹不返回 QML，也不保存原始凭据。
    parts = [account.get(k, "") for k in ("providerId", "credential", "apiUrl", "authMode", "workspace")]
    return hashlib.sha256(json.dumps(parts, separators=(",", ":")).encode()).hexdigest()


def fetch(account, http):
    provider = account["providerId"]
    credential = account.get("credential", "").strip()
    if not credential:
        return result(configured=False)
    base = account.get("apiUrl", "").rstrip("/")
    if provider == "deepseek":
        body = http(base or "https://api.deepseek.com/user/balance", headers=bearer(credential))
        balances = [{"currency": v.get("currency"), "balance": str(money(v.get("total_balance")))}
                    for v in body.get("balance_infos", []) if v.get("currency") in {"CNY", "USD"}]
        if not balances:
            raise RequestError("invalid_response")
        return {**result(), "status": "ok", "balances": balances}
    if provider == "glm":
        parsed = urllib.parse.urlsplit(base or "https://open.bigmodel.cn")
        value = zcode_quota(http(parsed.scheme + "://" + parsed.netloc + "/api/monitor/usage/quota/limit", headers={"Authorization": credential}))
        value["plan"] = "GLM Coding Plan"
        return value
    if account.get("authMode", "key") == "key":
        return go_windows(http((base or "https://opencode.ai") + "/zen/go/v1/usage", headers=bearer(credential)))
    # 完整 Cookie 头与单独 auth 值均接受，迁移时不拼接出 auth=auth=。
    cookie = credential if "=" in credential else "auth=" + credential
    site = base or "https://opencode.ai"
    workspace = account.get("workspace", "").strip()
    if not workspace:
        import re
        raw = http(site + "/", headers={"Cookie": cookie}, raw=True).decode("utf-8", "replace")
        ids = re.findall(r"wrk_[A-Za-z0-9_-]+", raw)
        if not ids:
            raise RequestError("workspace_required")
        workspace = ids[0]
    raw = http(site + "/workspace/" + urllib.parse.quote(workspace, safe="") + "/go", headers={"Cookie": cookie}, raw=True).decode("utf-8", "replace")
    return go_html(raw)


class Accounts:
    def __init__(self, cache_dir, http_factory=None):
        self.path = Path(cache_dir) / "accounts.sqlite3"
        self.http_factory = http_factory or (lambda seconds: Http(seconds))

    def collect(self, accounts, force=(), quota_enabled=True, now=None):
        now = now or datetime.now().astimezone()
        self.path.parent.mkdir(parents=True, exist_ok=True)
        connection = sqlite3.connect(self.path, timeout=5)
        connection.execute("CREATE TABLE IF NOT EXISTS state (id TEXT PRIMARY KEY, payload TEXT NOT NULL)")
        saved = {key: json.loads(value) for key, value in connection.execute("SELECT id,payload FROM state")}
        groups, values, out = {}, {}, queue.Queue()
        enabled = [a for a in accounts if a.get("enabled", True)]
        for account in enabled:
            if not quota_enabled and account["providerId"] != "deepseek":
                continue
            key = account_key(account)
            groups.setdefault(key, []).append(account)

        def run(key, account):
            try:
                out.put((key, fetch(account, self.http_factory(account.get("timeoutSec", 10)))))
            except RequestError as exc:
                out.put((key, {**result(status="reauth" if str(exc) == "reauth" else "unavailable"), "errorCode": str(exc) if str(exc) in {"reauth", "workspace_required"} else "unavailable"}))
            except Exception:
                out.put((key, result(status="unavailable")))

        pending = set()
        for key, group in groups.items():
            cached = saved.get("query:" + key, {})
            due = min(a.get("intervalSec", 300) for a in group)
            if cached and time.time() - cached.get("at", 0) < due and "*" not in force and not any(a["id"] in force for a in group):
                values[key] = cached["value"]
            else:
                pending.add(key)
                threading.Thread(target=run, args=(key, group[0]), daemon=True).start()
        deadline = time.monotonic() + 32
        while pending:
            try:
                key, value = out.get(timeout=max(0, deadline - time.monotonic()))
            except queue.Empty:
                break
            pending.discard(key)
            old = saved.get("query:" + key, {}).get("value", {})
            if time.time() * 1000 - timestamp(old.get("capturedAt")) > 12 * 3600 * 1000:
                old = {}
            if value["status"] == "unavailable" and old.get("status") == "ok":
                value = {**old, "stale": True}
            values[key] = value
            saved["query:" + key] = {"at": time.time(), "value": value}
        for key in pending:
            old = saved.get("query:" + key, {}).get("value")
            values[key] = {**old, "stale": True} if old else result(status="unavailable")
        answer = []
        for account in enabled:
            key = account_key(account)
            value = copy.deepcopy(values.get(key) or result(status="disabled"))
            for balance in value.get("balances", []):
                state_key = "balance:" + account["id"] + ":" + key + ":" + balance["currency"]
                previous = saved.get(state_key)
                if not value.get("stale") and (not previous or previous.get("capturedAt") != value.get("capturedAt")):
                    legacy = account.get("legacyState") if balance["currency"] == "CNY" else None
                    previous = observe(previous, money(balance["balance"]), now, account, legacy)
                    previous["capturedAt"] = value.get("capturedAt")
                    saved[state_key] = previous
                if previous and previous["day"] == business_day(now, account):
                    balance.update(spendToday=previous["spent"], affected=previous["affected"], since=previous["since"], estimated=True)
            answer.append({**value, "id": account["id"], "providerId": account["providerId"], "name": account.get("name") or account["providerId"]})
        with connection:
            for key, payload in saved.items():
                connection.execute("INSERT OR REPLACE INTO state VALUES (?,?)", (key, json.dumps(payload, ensure_ascii=False)))
        connection.close()
        return answer
