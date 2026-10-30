# quota.py — 原生额度适配器、白名单窗口和各提供方独立失败状态
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import queue
import threading
import time
import tomllib
import urllib.parse

from .common import array, database, first, integer, iso, number, obj, read_json, timestamp
from .network import Http, RequestError, bearer, jwt, persist_credentials


def window(key, used, reset=None, seconds=0):
    try:
        value = float(used) if used is not None and not isinstance(used, bool) else float("nan")
    except (TypeError, ValueError, OverflowError):
        value = float("nan")
    if not math.isfinite(value):
        return None
    when = timestamp(reset)
    try:
        reset_text = iso(when) if when > 0 else ""
    except (ValueError, OverflowError, OSError):
        reset_text = ""
    return {"id": key, "used": min(100, max(0, value)), "resetAt": reset_text, "seconds": integer(seconds)}


def fraction(key, raw, seconds=0):
    raw = obj(raw)
    limit = first(raw, "limit", "cap", "total", "entitlement")
    used = raw.get("used")
    try:
        limit = float(limit)
        if not math.isfinite(limit) or limit <= 0:
            return None
        if used is None and raw.get("remaining") is not None:
            remaining = float(raw["remaining"])
            if not math.isfinite(remaining):
                return None
            used = max(0, limit - remaining)
        used = float(used)
        if not math.isfinite(used):
            return None
    except (TypeError, ValueError, OverflowError):
        return None
    return window(key, used / limit * 100, first(raw, "resetTime", "resetAt", "reset_at", "resets_at", "reset"), seconds)


def result(windows=(), plan="", configured=True, status=None):
    windows = [entry for entry in windows if entry]
    return {"configured": configured, "status": status or ("not_configured" if not configured else "ok" if windows else "no_quota"),
            "windows": windows, "plan": str(plan or "")[:100], "stale": False, "capturedAt": iso(time.time() * 1000)}


def read_toml(path):
    try:
        return tomllib.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def codex_windows(group, spark=False):
    keys = ("primary_window", "secondary_window")
    raw = [obj(group.get(k)) for k in keys]
    values = [window(keys[i], v.get("used_percent"), v.get("reset_at"), v.get("limit_window_seconds")) for i, v in enumerate(raw)]
    kinds = ["primary_window" if v and v["seconds"] == 18000 else "secondary_window" if v and v["seconds"] == 604800 else None for v in values]
    selected = {}
    for kind, value in zip(kinds, values):
        if kind and kind not in selected:
            selected[kind] = value
    if not selected:
        selected = {k: v for k, v in zip(keys, values) if v}
    elif spark:
        for index, value in enumerate(values):
            other = kinds[1 - index]
            if value and not kinds[index] and other:
                slot = "secondary_window" if other == "primary_window" else "primary_window"
                if index == 1 or slot == "primary_window" or raw[index].get("limit_window_seconds") in (None, ""):
                    selected.setdefault(slot, value)
    return [{**value, "id": ("spark_" if spark else "") + key} for key, value in selected.items()]


class Quotas:
    def __init__(self, home=None, env=None, http=None, cache_dir=None, timeout=35):
        self.home = Path(home or Path.home())
        self.env = dict(os.environ if env is None else env)
        self.http = http or Http()
        self.cache_dir = Path(cache_dir) if cache_dir else None
        self.timeout = timeout

    def root(self, variable, fallback):
        from .discovery import expand
        return expand(self.env.get(variable) or fallback, self.home, self.env)

    def claude(self):
        path = self.root("CLAUDE_CONFIG_DIR", ".claude") / ".credentials.json"
        credentials = obj(read_json(path, {}))
        auth = obj(credentials.get("claudeAiOauth"))
        token = auth.get("accessToken")
        if not token:
            return result(configured=path.is_file(), status="reauth" if path.is_file() else "not_configured")
        expires = timestamp(auth.get("expiresAt"))
        if expires and expires <= time.time() * 1000 + 60000:
            raise RequestError("reauth")
        body = self.http("https://api.anthropic.com/api/oauth/usage", headers={**bearer(token), "anthropic-beta": "oauth-2025-04-20"})
        windows = []
        for key, value in body.items():
            if not isinstance(value, dict):
                continue
            if key == "extra_usage" and value.get("is_enabled"):
                windows.append(fraction("extra_window", {"limit": value.get("monthly_limit"), "used": value.get("used_credits")}))
            elif key.startswith(("five_hour", "seven_day")):
                name = {"five_hour": "primary_window", "seven_day": "secondary_window", "seven_day_opus": "opus_window"}.get(key, key + "_window")
                windows.append(window(name, value.get("utilization"), value.get("resets_at"), 18000 if key == "five_hour" else 604800))
        return result(windows, auth.get("subscriptionType"))

    def codex(self):
        path = self.root("CODEX_HOME", ".codex") / "auth.json"
        auth = obj(read_json(path, {}))
        tokens = obj(auth.get("tokens"))
        token = tokens.get("access_token")
        if not token:
            return result(configured=False)
        expiry = number(jwt(token).get("exp"))
        stale = expiry <= time.time() + 300 if expiry else time.time() * 1000 - timestamp(auth.get("last_refresh")) > 8 * 86400 * 1000
        if stale and tokens.get("refresh_token"):
            fresh = self.http("https://auth.openai.com/oauth/token", body={"client_id": "app_EMoamEEZ73f0CkXaXp7hrann", "grant_type": "refresh_token", "refresh_token": tokens["refresh_token"]})
            if not fresh.get("access_token"):
                raise RequestError("reauth")
            updated = {**auth, "tokens": {**tokens, **{key: fresh[key] for key in ("access_token", "refresh_token", "id_token") if fresh.get(key)}}, "last_refresh": iso(time.time() * 1000)}
            persist_credentials(path, auth, updated)
            tokens = updated["tokens"]
            token = tokens["access_token"]
        headers = bearer(token)
        account = tokens.get("account_id") or obj(jwt(token).get("https://api.openai.com/auth")).get("chatgpt_account_id")
        if account:
            headers["ChatGPT-Account-Id"] = account
        body = self.http("https://chatgpt.com/backend-api/wham/usage", headers=headers)
        windows = codex_windows(obj(body.get("rate_limit")))
        spark = {}
        for group in array(body.get("additional_rate_limits")):
            if "spark" in str(group).lower():
                for entry in codex_windows(obj(group.get("rate_limit")), spark=True):
                    spark.setdefault(entry["id"], entry)
        windows.extend(spark.values())
        credit = obj(obj(body.get("spend_control")).get("individual_limit"))
        percent = credit.get("used_percent")
        derived = fraction("credit_window", credit)
        windows.append(derived if derived and (percent is None or number(percent) == 0 and number(credit.get("used")) > 0) else window("credit_window", percent, credit.get("reset_at")))
        return result(windows, body.get("plan_type") or obj(jwt(token).get("https://api.openai.com/auth")).get("chatgpt_plan_type") or obj(jwt(tokens.get("id_token")).get("https://api.openai.com/auth")).get("chatgpt_plan_type"))

    def cursor_auth(self):
        from .account_usage import cursor_auth
        return cursor_auth(self.home, self.env)

    def cursor(self):
        headers = self.cursor_auth()
        if not headers:
            return result(configured=False)
        from .account_usage import CursorHttp
        summary_http = CursorHttp(self.http.timeout) if type(self.http) is Http else self.http
        body = summary_http("https://cursor.com/api/usage-summary", headers=headers)
        individual = obj(body.get("individualUsage"))
        plan = obj(individual.get("plan"))
        percent = plan.get("totalPercentUsed")
        parts = [plan[k] for k in ("autoPercentUsed", "apiPercentUsed") if plan.get(k) is not None]
        if percent is None and parts:
            percent = sum(number(v) for v in parts) / len(parts)
        for raw in (plan, obj(individual.get("onDemand")), obj(obj(body.get("teamUsage")).get("onDemand"))):
            if (percent is None or number(percent) == 0) and number(raw.get("limit")) > 0:
                percent = number(raw.get("used")) / number(raw["limit"]) * 100
        reset = body.get("billingCycleEnd")
        seconds = max(0, timestamp(reset) - timestamp(body.get("billingCycleStart"))) // 1000
        values = [window("primary_window", percent, reset, seconds), window("secondary_window", plan.get("autoPercentUsed"), reset, seconds),
                  window("tertiary_window", plan.get("apiPercentUsed"), reset, seconds)]
        try:
            from .quota_products import cursor_sand
            values.append(cursor_sand(self))
        except (RequestError, ValueError, OSError):
            pass
        return result(values, body.get("membershipType"))

    def kimi(self):
        root = self.root("KIMI_HOME", ".kimi-code")
        if self.env.get("KIMI_CODE_HOME") and not self.env.get("KIMI_HOME"):
            root = Path(self.env["KIMI_CODE_HOME"])
        if not (root / "config.toml").is_file() and not (root / "credentials/kimi-code.json").is_file() and not any(self.env.get(k) for k in ("KIMI_HOME", "KIMI_CODE_HOME", "KIMI_CODE_BASE_URL", "KIMI_CODE_OAUTH_HOST", "KIMI_OAUTH_HOST")):
            root = self.home / ".kimi"
        config = obj(read_toml(root / "config.toml").get("providers")).get("managed:kimi-code", {})
        if obj(config.get("oauth")).get("storage") not in (None, "file"):
            raise RequestError("unsupported_credentials")
        base = str(self.env.get("KIMI_CODE_BASE_URL") or config.get("base_url") or "https://api.kimi.com/coding/v1").rstrip("/")
        oauth = str(self.env.get("KIMI_CODE_OAUTH_HOST") or self.env.get("KIMI_OAUTH_HOST") or obj(config.get("oauth")).get("oauth_host") or "https://auth.kimi.com").rstrip("/")
        if (base, oauth) not in {("https://api.kimi.com/coding/v1", "https://auth.kimi.com"), ("https://api.kimi.ai/coding/v1", "https://auth.kimi.ai")}:
            raise RequestError("invalid_endpoint")
        slot = "kimi-code-env-0e4f99c69cc27850" if oauth.endswith(".ai") else "kimi-code"
        path = root / "credentials" / (slot + ".json")
        auth = obj(read_json(path, {}))
        if not auth.get("access_token"):
            return result(configured=False)
        token = auth["access_token"]
        def refresh():
            nonlocal auth, token
            fresh = self.http(oauth + "/api/oauth/token", form={"client_id": "17e5f671-d194-4dfb-9706-5516cb48c098", "grant_type": "refresh_token", "refresh_token": auth["refresh_token"]}, headers={"X-Msh-Platform": "kimi_cli"})
            if not fresh.get("access_token"):
                raise RequestError("reauth")
            next_auth = {**auth, **fresh, "expires_at": time.time() + (number(fresh.get("expires_in")) or 900)}
            persist_credentials(path, auth, next_auth)
            auth, token = next_auth, fresh["access_token"]
        if timestamp(auth.get("expires_at")) < time.time() * 1000 and auth.get("refresh_token"):
            refresh()
        try:
            body = self.http(base + "/usages", headers=bearer(token))
        except RequestError as error:
            if str(error) != "reauth" or not auth.get("refresh_token"):
                raise
            refresh()
            body = self.http(base + "/usages", headers=bearer(token))
        limit = next(iter(array(body.get("limits"))), {})
        return result([fraction("primary_window", body.get("usage")), fraction("secondary_window", limit.get("detail") or limit),
                       fraction("tertiary_window", body.get("totalQuota"))], obj(obj(body.get("user")).get("membership")).get("level") or body.get("subType"))

    def gemini(self):
        root = self.root("GEMINI_HOME", ".gemini")
        auth = obj(read_json(root / "oauth_creds.json", {}))
        if not auth.get("access_token"):
            return result(configured=False)
        expiry = timestamp(auth.get("expiry_date"))
        if expiry and expiry < time.time() * 1000 + 60000 and auth.get("refresh_token"):
            fresh = self.http("https://oauth2.googleapis.com/token", form={
                "client_id": "681255809395-oo8ft2oprdrnp9e3aqf6av3hmdib135j.apps.googleusercontent.com",
                "client_secret": "-".join(("GOCSPX", "4uHgMPm", "1o7Sk", "geV6Cu5clXFsxl")),
                "refresh_token": auth["refresh_token"], "grant_type": "refresh_token"})
            if not fresh.get("access_token"):
                raise RequestError("reauth")
            updated = {**auth, **{k: fresh[k] for k in ("access_token", "id_token", "refresh_token") if fresh.get(k)},
                       "expiry_date": time.time() * 1000 + number(fresh.get("expires_in")) * 1000}
            persist_credentials(root / "oauth_creds.json", auth, updated)
            auth = updated
        headers = bearer(auth["access_token"])
        assist = self.http("https://cloudcode-pa.googleapis.com/v1internal:loadCodeAssist", headers=headers,
                           body={"metadata": {"ideType": "GEMINI_CLI", "pluginType": "GEMINI"}})
        project = assist.get("cloudaicompanionProject")
        if isinstance(project, dict):
            project = project.get("id") or project.get("projectId")
        body = self.http("https://cloudcode-pa.googleapis.com/v1internal:retrieveUserQuota", headers=headers, body={"project": project} if project else {})
        buckets = {}
        for raw in array(body.get("buckets")):
            model = str(raw.get("modelId") or "")
            if raw.get("remainingFraction") is None:
                continue
            key = "tertiary_window" if "flash-lite" in model else "secondary_window" if "flash" in model else "primary_window"
            candidate = window(key, 100 - number(raw["remainingFraction"]) * 100, raw.get("resetTime"))
            if key not in buckets or candidate["used"] > buckets[key]["used"]:
                buckets[key] = candidate
        return result(buckets.values(), obj(assist.get("currentTier")).get("id"))

    def command_code(self):
        token = self.env.get("COMMAND_CODE_API_KEY") or self.env.get("COMMANDCODE_API_KEY") or obj(read_json(self.root("COMMANDCODE_HOME", ".commandcode") / "auth.json", {})).get("apiKey")
        if not token:
            return result(configured=False)
        headers = bearer(token)
        base = "https://api.commandcode.ai"
        who = self.http(base + "/alpha/whoami?limits=1", headers=headers)
        org = obj(obj(who.get("data") or who).get("org")).get("id")
        suffix = "?" + urllib.parse.urlencode({"orgId": org}) if org else ""
        credits = self.http(base + "/alpha/billing/credits" + suffix, headers=headers)
        subscription = self.http(base + "/alpha/billing/subscriptions" + suffix, headers=headers)
        plan = obj(subscription.get("data") or subscription)
        windows = obj(credits.get("windowLimits"))
        return result([fraction("primary_window", windows.get("fiveHour") or windows.get("five_hour"), 18000),
                       fraction("secondary_window", windows.get("weekly"), 604800)], plan.get("planId"))

    def devin(self):
        auth = read_toml(self.root("XDG_DATA_HOME", ".local/share") / "devin/credentials.toml")
        token = auth.get("windsurf_api_key")
        if not token:
            return result(configured=False)
        if auth.get("api_server_url", "https://server.codeium.com").rstrip("/") != "https://server.codeium.com":
            raise RequestError("invalid_endpoint")
        body = self.http("https://server.codeium.com/exa.seat_management_pb.SeatManagementService/GetPlanStatus",
                         headers={"Connect-Protocol-Version": "1", "x-auth-token": token}, body={})
        status = obj(body.get("planStatus"))
        if not status:
            raise RequestError("invalid_response")
        plan = obj(status.get("planInfo"))
        windows = []
        if plan.get("billingStrategy") in {None, "BILLING_STRATEGY_QUOTA"}:
            for period, key, seconds in (("daily", "primary_window", 86400), ("weekly", "secondary_window", 604800)):
                reset = status.get(period + "QuotaResetAtUnix")
                remaining = status.get(period + "QuotaRemainingPercent")
                if period + "QuotaRemainingPercent" in status and window(key, remaining) is None:
                    raise RequestError("invalid_response")
                if plan.get("hide" + period.title() + "Quota") or (remaining is None and not timestamp(reset)):
                    continue
                windows.append(window(key, 100 - number(remaining), reset, seconds))
        return result(windows, plan.get("planName"))

    def collect(self, devin_enabled=False):
        from . import quota_local, quota_products
        providers = {
            "claude": self.claude, "codex": self.codex, "cursor": self.cursor,
            "kimi": self.kimi, "gemini": self.gemini, "commandCode": self.command_code,
            "devin": self.devin if devin_enabled else lambda: result(configured=False),
            "kiro": lambda: quota_local.kiro(self), "copilot": lambda: quota_local.copilot(self),
            "antigravity": lambda: quota_local.antigravity(self),
            "grok": lambda: quota_products.grok(self), "zcode": lambda: quota_products.zcode(self),
            "opencodeGo": lambda: quota_products.opencode_go(self),
            "qoder": lambda: quota_products.qoder(self), "qoderCn": lambda: quota_products.qoder(self, china=True),
            "codingPlan": lambda: quota_products.ark(self, "coding-plan"),
            "agentPlan": lambda: quota_products.ark(self, "agent-plan"),
            "workbuddy": lambda: quota_local.workbuddy(self),
        }
        completed = queue.Queue()
        values = {}
        cache = obj(read_json(self.cache_dir / "quota.json")) if self.cache_dir else {}
        cache = obj(cache.get("providers"))
        next_cache = {}
        now = time.time()
        identities = {key: self.credential_identity(key) for key in providers}
        def run(key, callback):
            try:
                value = callback()
                completed.put((key, value, None))
            except RequestError as error:
                completed.put((key, result(status="reauth" if str(error) == "reauth" else "unavailable"), error.retry_after))
            except Exception:
                # 账号、路径和原始 HTTP/IPC 错误不能越过白名单接口。
                completed.put((key, result(status="unavailable"), 0))
        # 各提供方独立采集；守护线程不会让超时请求拖住一次性采集进程退出。
        deadline = time.monotonic() + self.timeout
        for key, callback in providers.items():
            previous = obj(cache.get(key))
            if previous.get("identity") == identities[key] and number(previous.get("retryAt")) > now:
                values[key] = self.cached(previous.get("value")) or result(status="unavailable")
                next_cache[key] = previous
                continue
            threading.Thread(target=run, args=(key, callback), daemon=True).start()
        while len(values) < len(providers):
            try:
                key, value, retry = completed.get(timeout=max(0, deadline - time.monotonic()))
            except queue.Empty:
                break
            values[key] = value
            previous = obj(cache.get(key))
            if value["status"] == "unavailable" and previous.get("identity") == identities[key] == self.credential_identity(key):
                values[key] = self.cached(previous.get("value")) or value
            if values[key]["windows"]:
                next_cache[key] = {"identity": self.credential_identity(key), "value": values[key], "retryAt": now + min(86400, retry or 0)}
            elif retry:
                next_cache[key] = {"identity": identities[key], "value": value, "retryAt": now + min(86400, retry)}
        for key in providers:
            if key not in values:
                previous = obj(cache.get(key))
                values[key] = (self.cached(previous.get("value")) if previous.get("identity") == identities[key] else None) or result(status="unavailable")
                if values[key]["windows"]:
                    next_cache[key] = previous
        # Cursor 的 GrokBot 与 Grok 账单可能是同一池；相同占用及重置时间不重复展示。
        grok = next((w for w in values["grok"]["windows"] if w["id"] == "primary_window"), None)
        if grok:
            values["cursor"]["windows"] = [w for w in values["cursor"]["windows"] if not (w["id"] == "quaternary_window" and abs(w["used"] - grok["used"]) < 0.001 and w["resetAt"] == grok["resetAt"])]
        if self.cache_dir:
            self.cache_dir.mkdir(parents=True, exist_ok=True)
            destination = self.cache_dir / "quota.json"
            temporary = destination.with_name("quota-" + str(os.getpid()) + ".tmp")
            try:
                temporary.write_text(json.dumps({"version": 1, "providers": next_cache}, ensure_ascii=False), encoding="utf-8")
                os.chmod(temporary, 0o600)
                temporary.replace(destination)
            finally:
                temporary.unlink(missing_ok=True)
        return values

    def cached(self, value):
        if not isinstance(value, dict) or time.time() * 1000 - timestamp(value.get("capturedAt")) > 12 * 3600 * 1000:
            return None
        restored = result(plan=value.get("plan"))
        restored["capturedAt"] = value.get("capturedAt", "")
        restored["stale"] = True
        for raw in array(value.get("windows")):
            if timestamp(raw.get("resetAt")) and timestamp(raw.get("resetAt")) <= time.time() * 1000:
                continue
            entry = window(str(raw.get("id", "")), raw.get("used"), raw.get("resetAt"), raw.get("seconds"))
            if entry and entry["id"].endswith("_window"):
                restored["windows"].append(entry)
        if not restored["windows"]:
            return None
        restored["status"] = "ok"
        return restored

    def credential_identity(self, provider):
        # 指纹只用于换号失效，不存储账号、令牌、路径或原始凭据。
        roots = {
            "claude": [self.root("CLAUDE_CONFIG_DIR", ".claude") / ".credentials.json"],
            "codex": [self.root("CODEX_HOME", ".codex") / "auth.json"],
            "cursor": [self.home / ".cursor/cli-config.json", self.root("APPDATA", "AppData/Roaming") / "Cursor/User/globalStorage/state.vscdb"],
            "kimi": [self.root("KIMI_HOME", self.env.get("KIMI_CODE_HOME") or ".kimi-code") / "credentials" / n for n in ("kimi-code.json", "kimi-code-env-0e4f99c69cc27850.json")] + [self.home / ".kimi/credentials/kimi-code.json"],
            "gemini": [self.root("GEMINI_HOME", ".gemini") / "oauth_creds.json"],
            "grok": [self.root("GROK_HOME", ".grok") / "auth.json"],
            "zcode": [self.root("ZCODE_HOME", ".zcode") / "v2/credentials.json", self.root("ZCODE_HOME", ".zcode") / "v2/config.json"],
            "commandCode": [self.home / ".commandcode/auth.json"],
            "devin": [self.root("XDG_DATA_HOME", ".local/share") / "devin/credentials.toml"],
            "copilot": [self.home / ".config/github-copilot" / n for n in ("apps.json", "hosts.json", "auth.db")],
            "codingPlan": list((self.home / ".arkcli").glob("*.json")) + list((self.home / ".arkcli").glob("*.yaml")),
            "agentPlan": list((self.home / ".arkcli").glob("*.json")) + list((self.home / ".arkcli").glob("*.yaml")),
            "workbuddy": [self.root(env, default) / "wbipc/endpoint.json" for env, default in (("WORKBUDDY_HOME", ".workbuddy"), ("WORKBUDDY_AI_HOME", ".workbuddy-ai"))],
            "antigravity": [self.home / ".gemini" / n for n in ("jetski-standalone-oauth-token", "antigravity/jetski-standalone-oauth-token", "antigravity/antigravity-oauth-token", "antigravity-ide/antigravity-oauth-token", "antigravity-cli/antigravity-oauth-token")],
            "qoder": [self.root("QODER_HOME", str(self.root("APPDATA", "AppData/Roaming") / "Qoder")) / "SharedClientCache/.info.json"],
            "qoderCn": [self.root("QODER_CN_HOME", str(self.root("APPDATA", "AppData/Roaming") / "QoderCN")) / "SharedClientCache/.info.json", self.root("QODERCN_CONFIG_DIR", ".qoder-cn") / ".auth/user"],
        }.get(provider, [])
        if provider == "workbuddy":
            for directory in (self.root("APPDATA", "AppData/Roaming") / "CodeBuddyExtension/Data/Public/auth", self.root("WORKBUDDY_HOME", ".workbuddy") / "auth", self.root("WORKBUDDY_AI_HOME", ".workbuddy-ai") / "auth"):
                roots.extend(directory / name for name in ("workbuddy-desktop.info", "workbuddy-ai.info"))
        digest = hashlib.sha256()
        digest.update(provider.encode())
        for path in roots:
            try:
                with path.open("rb") as stream:
                    digest.update(stream.read(1024 * 1024))
            except OSError:
                digest.update(b"missing")
        for key, value in sorted(self.env.items()):
            if key.startswith(("OPENCODE_GO_", "COMMAND_CODE_", "WORKBUDDY_", "QODER", "GROK_", "ZCODE_", "KIMI_")):
                digest.update(key.encode() + b"=" + str(value).encode())
        return digest.hexdigest()
