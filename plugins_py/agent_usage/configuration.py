# configuration.py — Token Usage 列布局、账户配置与旧版设置迁移
from __future__ import annotations

import copy
import hashlib
import urllib.parse

METRICS = {"tokens", "quota5h", "quota7d", "spendToday", "balance", "window", "reset", "empty"}
PROVIDERS = {"deepseek", "glm", "opencode-go"}


def block(metric="empty", target="", **extra):
    return {"metric": metric, "targetId": target, **extra}


def column(key, single=None, top=None, bottom=None):
    return {"id": key, "layout": "single" if single else "split", "single": single or block(),
            "top": top or block(), "bottom": bottom or block()}


def default_columns(config):
    from model import BAR_QUOTA_NAMES
    columns = [column("total", block("tokens", "total", label=config.get("barPrefix", "")))]
    for key in BAR_QUOTA_NAMES:
        mode = config.get("barQuota_" + key, "none")
        if not isinstance(mode, str) or mode not in {"none", "5h", "7d", "both"}:
            raise ValueError("invalid_bar_quota_mode")
        if mode != "none":
            target = "auto:" + key
            columns.append(column("quota-" + key, top=block("quota5h", target) if mode in {"5h", "both"} else None,
                                  bottom=block("quota7d", target) if mode in {"7d", "both"} else None))
    return columns


def migrate(config):
    result = copy.deepcopy(config or {})
    accounts = result.setdefault("accounts", [])
    had_columns = "barColumns" in result
    columns = result.setdefault("barColumns", default_columns(result))
    for index, vendor in enumerate(result.get("legacyVendors", [])):
        key = vendor.get("secretId") or hashlib.sha256((str(index) + str(vendor.get("id", ""))).encode()).hexdigest()[:24]
        account_id = "legacy-" + key
        if any(a.get("id") == account_id for a in accounts):
            continue
        provider = {"balance": "deepseek", "plan": "glm", "go": "opencode-go"}.get(vendor.get("mode", "balance"))
        if not provider:
            continue
        account = {**vendor, "id": account_id, "providerId": provider, "credential": vendor.get("apiKey", ""),
                   "authMode": "cookie" if provider == "opencode-go" else "key", "startHour": vendor.get("startHour", 8),
                   "startMin": vendor.get("startMin", 30)}
        account.pop("apiKey", None)
        accounts.append(account)
        if not had_columns and account.get("enabled", True):
            columns.append(column("account-" + key, top=block("spendToday" if provider == "deepseek" else "quota5h", account_id),
                                  bottom=block("balance" if provider == "deepseek" else "quota7d", account_id)))
    for account in result.get("legacyBarAccounts", []):
        if not had_columns:
            key = account["id"]
            if not any(c["id"] == "account-" + key for c in columns):
                columns.append(column("account-" + key, top=block("spendToday" if account["providerId"] == "deepseek" else "quota5h", key), bottom=block("balance" if account["providerId"] == "deepseek" else "quota7d", key)))
    result.pop("legacyBarAccounts", None)
    result.pop("legacyVendors", None)
    return result


def validate_layout(config):
    columns = config.get("barColumns", default_columns(config))
    if not isinstance(columns, list) or len(columns) > 32:
        raise ValueError("invalid_bar_columns")
    seen = set()
    for col in columns:
        if not isinstance(col, dict) or not isinstance(col.get("id"), str) or not col["id"] or col["id"] in seen or col.get("layout") not in {"single", "split"}:
            raise ValueError("invalid_bar_column")
        seen.add(col["id"])
        for slot in ("single", "top", "bottom"):
            value = col.get(slot, block())
            if not isinstance(value, dict) or value.get("metric") not in METRICS or not isinstance(value.get("targetId", ""), str) or not isinstance(value.get("label", ""), str) or not isinstance(value.get("windowId", ""), str):
                raise ValueError("invalid_bar_block")
    return copy.deepcopy(columns)


def validate_accounts(config):
    accounts = config.get("accounts", [])
    if not isinstance(accounts, list) or len(accounts) > 32:
        raise ValueError("invalid_accounts")
    seen = set()
    for account in accounts:
        if not isinstance(account, dict) or not isinstance(account.get("id"), str) or not account["id"] or account["id"].startswith("auto:") or account["id"] in seen or account.get("providerId") not in PROVIDERS:
            raise ValueError("invalid_account")
        seen.add(account["id"])
        if not isinstance(account.get("credential", ""), str) or account.get("authMode", "key") not in {"key", "cookie"}:
            raise ValueError("invalid_account_credentials")
        for key, low, high, default in (("intervalSec", 10, 86400, 300), ("timeoutSec", 1, 30, 10), ("startHour", 0, 23, 0), ("startMin", 0, 59, 0)):
            value = account.get(key, default)
            if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
                raise ValueError("invalid_account_" + key)
        url = account.get("apiUrl", "")
        if url:
            parsed = urllib.parse.urlsplit(url)
            if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password or parsed.fragment:
                raise ValueError("invalid_account_endpoint")
    return copy.deepcopy(accounts)
