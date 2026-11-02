# quota_products.py — 云套餐、原生 Ark CLI 与 Qoder 本机服务的额度适配
# 协议参考 TokenTracker 1.5.1（MIT），归属见 THIRD_PARTY_NOTICES.md。
from __future__ import annotations

import base64
import getpass
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import time
import urllib.parse
import uuid

from .common import array, first, iso, number, obj, read_json, timestamp
from .network import RequestError, bearer, persist_credentials
from .quota import fraction, result, window
from .quota_local import LocalStream, command

SLOTS = ("primary_window", "secondary_window", "tertiary_window", "quaternary_window")


def proto_fields(data):
    if len(data) > 65536:
        raise RequestError("response_too_large")
    offset = 0
    def varint():
        nonlocal offset
        value = 0
        for shift in range(0, 70, 7):
            if offset >= len(data):
                raise RequestError("invalid_response")
            byte = data[offset]
            offset += 1
            value |= (byte & 127) << shift
            if byte < 128:
                return value
        raise RequestError("invalid_response")
    fields = {}
    while offset < len(data):
        tag = varint()
        field, wire = tag >> 3, tag & 7
        if wire == 0:
            value = varint()
        elif wire in (1, 2, 5):
            size = varint() if wire == 2 else 8 if wire == 1 else 4
            if offset + size > len(data):
                raise RequestError("invalid_response")
            value = data[offset:offset + size]
            offset += size
        else:
            raise RequestError("invalid_response")
        fields[field] = (wire, value)
    return fields


def cursor_sand(q):
    from .account_usage import cursor_credentials
    auth = cursor_credentials(q.home, q.env)
    if not auth:
        return None
    headers = {**bearer(auth["accessToken"]), "Content-Type": "application/proto", "Connect-Protocol-Version": "1"}
    base = "https://api2.cursor.sh/aiserver.v1.DashboardService/"
    access = proto_fields(q.http(base + "GetSandAccessStatus", headers=headers, raw_body=b"", raw=True))
    if access.get(1) != (0, 1):
        return None
    usage = proto_fields(q.http(base + "GetSandUsageStatus", headers=headers, raw_body=b"", raw=True))
    if usage.get(4) == (0, 1) or usage.get(3, (None,))[0] != 1:
        return None
    percent = struct.unpack("<d", usage[3][1])[0]
    reset = proto_fields(usage[2][1]).get(1, (0, 0))[1] if 2 in usage else 0
    start = proto_fields(usage[1][1]).get(1, (0, 0))[1] if 1 in usage else 0
    if not reset:
        return None
    return window("quaternary_window", percent, reset, max(0, reset - start) if start else 0)


def b64(value):
    return base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))


def grok_body(body):
    config = obj(body.get("config"))
    if not config:
        raise RequestError("invalid_response")
    period = obj(config.get("currentPeriod"))
    reset = period.get("end") or config.get("billingPeriodEnd")
    used = config.get("creditUsagePercent")
    products = config.get("productUsage")
    if used is None and products:
        values = products if isinstance(products, list) else list(obj(products).values())
        usable = [v.get("usagePercent") for v in values if isinstance(v, dict) and v.get("usagePercent") is not None]
        if usable:
            used = sum(number(v) for v in usable)
    if used is None and number(config.get("monthlyLimit")):
        used = number(config.get("used")) / number(config["monthlyLimit"]) * 100
    if used is None and period and not products and config.get("creditUsagePercent") is None:
        used = 0
    values = [window("primary_window", used, reset), fraction("secondary_window", {
        "limit": config.get("onDemandCap"), "used": config.get("onDemandUsed"), "resetAt": reset})]
    plan = str(body.get("subscriptionTier") or "").replace("SUBSCRIPTION_TIER_", "").replace("_", " ").title()
    return result(values, plan)


def grok(q):
    path = q.root("GROK_HOME", ".grok") / "auth.json"
    original = obj(read_json(path))
    candidates = [(k, v) for k, v in original.items() if isinstance(v, dict) and (v.get("key") or v.get("refresh_token"))]
    if not candidates:
        return result(configured=False)
    key, auth = next((v for v in candidates if v[1].get("key")), candidates[0])
    token = auth.get("key")
    def refresh():
        nonlocal auth, token, original
        client = auth.get("oidc_client_id") or (key.rsplit("::", 1)[1] if "::" in key else None)
        if not client or not auth.get("refresh_token"):
            raise RequestError("reauth")
        # 登录文件不能将 refresh token 重定向到任意站点。
        endpoint = q.env.get("GROK_OIDC_TOKEN_ENDPOINT") or "https://auth.x.ai/oauth2/token"
        if urllib.parse.urlsplit(endpoint).hostname != "auth.x.ai":
            raise RequestError("invalid_endpoint")
        fresh = q.http(endpoint, form={"grant_type": "refresh_token", "refresh_token": auth["refresh_token"], "client_id": client})
        token = fresh.get("access_token")
        if not token:
            raise RequestError("reauth")
        auth = {**auth, "key": token, "refresh_token": fresh.get("refresh_token") or auth["refresh_token"],
                "expires_at": iso(time.time() * 1000 + number(fresh.get("expires_in")) * 1000)}
        updated = {**original, key: auth}
        persist_credentials(path, original, updated)
        original = updated
    expiry = timestamp(auth.get("expires_at"))
    if not token or expiry and expiry <= time.time() * 1000 + 60000:
        refresh()
    base = "https://cli-chat-proxy.grok.com"
    try:
        body = q.http(base + "/v1/billing?format=credits", headers=bearer(token))
    except RequestError as error:
        if str(error) == "reauth" and auth.get("refresh_token"):
            refresh()
            body = q.http(base + "/v1/billing?format=credits", headers=bearer(token))
        else:
            body = q.http(base + "/v1/billing", headers=bearer(token))
    try:
        value = grok_body(body)
    except RequestError:
        value = grok_body(q.http(base + "/v1/billing", headers=bearer(token)))
    if not value["plan"]:
        try:
            settings = q.http(base + "/v1/settings", headers=bearer(token))
            value["plan"] = str(first(settings, "subscription_tier_display", "subscription_tier", default=""))[:100]
        except RequestError:
            pass
    return value


def go_windows(payload):
    values = []
    for index, key in enumerate(("rolling", "weekly", "monthly")):
        raw = obj(obj(payload.get("usage")).get(key) or payload.get(key) or payload.get(key + "Usage"))
        percent = first(raw, "percent", "usagePercent", "usage_percent")
        reset = first(raw, "resetsAt", "resets_at", "resetAt", "reset_at")
        seconds = first(raw, "resetInSec", "reset_in_sec")
        if seconds is not None:
            reset = time.time() + number(seconds)
        # weekly 明确是七天；rolling 与剩余重置时间不能证明是五小时窗口。
        entry = window(SLOTS[index], percent, reset, 604800 if key == "weekly" else 0)
        if entry:
            entry["label"] = key
        values.append(entry)
    if not any(values):
        raise RequestError("invalid_response")
    return result(values, "OpenCode Go")


def go_html(html):
    values = {}
    cleaned = html.replace('\\"', '"')
    for key in ("rolling", "weekly", "monthly"):
        match = re.search(key + r'Usage[^{}]{0,30}\{[^{}]{0,1000}\}', cleaned)
        if match:
            pct = re.search(r'usagePercent["\s:]+([\d.]+)', match[0])
            reset = re.search(r'resetInSec["\s:]+([\d.]+)', match[0])
            if pct:
                values[key] = {"percent": float(pct[1]), "resetInSec": float(reset[1]) if reset else 0}
    for part in html.split('data-slot="usage-item"')[1:]:
        label = re.search(r'data-slot="usage-label">([^<]+)<', part)
        percent = re.search(r'data-slot="usage-value">[^0-9]*(\d+(?:\.\d+)?)', part)
        reset = re.search(r'data-slot="(reset-time|reset-now)">([\s\S]*?)</span>', part)
        if not label or not percent or not reset:
            continue
        key = next((k for k in ("rolling", "weekly", "monthly") if k in label[1].lower()), None)
        if key and key not in values:
            text = re.sub(r"<!--.*?-->", "", reset[2])
            seconds = sum(number(n) * {"d": 86400, "h": 3600, "m": 60, "s": 1}[unit] for n, unit in re.findall(r"([\d.]+)\s*([dhms])", text.lower()))
            values[key] = {"percent": float(percent[1]), "resetInSec": seconds}
    value = result(plan="OpenCode Go") if not values and re.search(r"data-slot\s*=\s*['\"]subscribe-button['\"]", cleaned) else go_windows(values)
    value["subscribed"] = bool(re.search(r"您已订阅 OpenCode Go|You're subscribed to OpenCode Go", html))
    match = re.search(r"useBalance:\s*(![01]|true|false)", html)
    if match:
        value["useBalance"] = match[1] in {"!0", "true"}
    return value


def opencode_go(q):
    token = q.env.get("OPENCODE_GO_API_KEY")
    if not token:
        auth = obj(read_json(q.root("XDG_DATA_HOME", ".local/share") / "opencode/auth.json"))
        token = obj(auth.get("opencode-go")).get("key")
    if token:
        return go_windows(q.http("https://opencode.ai/zen/go/v1/usage", headers=bearer(token)))
    cookie = q.env.get("OPENCODE_GO_AUTH_COOKIE")
    if not cookie:
        return result(configured=False)
    workspace = q.env.get("OPENCODE_GO_WORKSPACE_ID")
    headers = {"Cookie": "auth=" + cookie, "Accept": "text/html"}
    if not workspace:
        server = "def39973159c7f0483d8793a822b8dbb10d067e12c65455fcb4608459ba0234f"
        server_headers = {**headers, "X-Server-Id": server, "X-Server-Instance": "server-fn:" + str(uuid.uuid4()), "Origin": "https://opencode.ai", "Referer": "https://opencode.ai"}
        raw = q.http("https://opencode.ai/_server?id=" + server, headers=server_headers, raw=True).decode("utf-8")
        ids = re.findall(r"wrk_[A-Za-z0-9_-]+", raw)
        if not ids and not re.search(r"sign.?in|unauthorized", raw, re.I):
            raw = q.http("https://opencode.ai/_server", headers=server_headers, body=[], raw=True).decode("utf-8")
            ids = re.findall(r"wrk_[A-Za-z0-9_-]+", raw)
        if not ids:
            raise RequestError("reauth")
        workspace = ids[0]
    raw = q.http("https://opencode.ai/workspace/" + urllib.parse.quote(workspace, safe="") + "/go", headers=headers, raw=True).decode("utf-8")
    try:
        return go_html(raw)
    except RequestError:
        if re.search(r"data-slot\s*=\s*['\"]subscribe-button['\"]", raw.replace('\\"', '"'), re.I):
            return result(plan="OpenCode Go")
        raise


def ark_binary(q):
    # npm 只是一层启动壳；直接执行其 bin/ 下的 Go 二进制，绝不启动 Node。
    candidate = shutil.which("arkcli.exe" if os.name == "nt" else "arkcli", path=q.env.get("PATH", ""))
    if candidate:
        path = Path(candidate)
        try:
            if path.read_bytes()[:2] == b"MZ" or os.name != "nt" and path.read_bytes()[:4] == b"\x7fELF":
                return path
        except OSError:
            pass
    roots = [q.root("APPDATA", "AppData/Roaming") / "npm", q.home / ".local/bin", q.home / ".arkcli/bin",
             q.home / ".npm-global", q.home / ".volta/tools/image/packages/@volcengine/ark-cli/lib"]
    roots.extend(Path(p) for p in q.env.get("PATH", "").split(os.pathsep) if p)
    seen = set()
    for root in roots:
        for path in (root / "node_modules/@volcengine/ark-cli/bin/arkcli-windows-amd64.exe",
                     root / "node_modules/@volcengine/ark-cli/bin/arkcli-windows-arm64.exe", root / "arkcli.exe"):
            if path in seen:
                continue
            seen.add(path)
            if path.is_file():
                return path
    return None


def ark(q, product):
    binary = ark_binary(q)
    if not binary:
        # 已安装的 npm 启动壳不能被当成可执行 CLI，避免隐式重新引入 Node。
        installed = (q.home / ".arkcli").is_dir() or (q.root("APPDATA", "AppData/Roaming") / "npm/node_modules/@volcengine/ark-cli").is_dir()
        installed = installed or any((Path(folder) / name).is_file() for folder in q.env.get("PATH", "").split(os.pathsep) if folder for name in ("arkcli", "arkcli.cmd", "arkcli.ps1"))
        return result(configured=installed, status="cli_missing" if installed else "not_configured")
    raw, code = command([str(binary), "usage", "plan", "--format", "json"], 10, q.env)
    if code:
        raise RequestError("reauth" if re.search("login|unauth|expired", raw, re.I) else "unavailable")
    try:
        body = json.loads(raw)
    except ValueError:
        raise RequestError("invalid_response") from None
    item = next((r for r in array(body.get("items")) if r.get("product") == product and r.get("subscribed") is not False), None)
    if not item:
        return result(configured=False)
    slots = {"5h": "primary_window", "session": "primary_window", "five_hour": "primary_window", "weekly": "secondary_window", "monthly": "tertiary_window"}
    windows = []
    for period in array(item.get("periods")):
        key = slots.get(period.get("label"))
        if key:
            used = period.get("percent")
            if used is None and number(period.get("total")):
                used = number(period.get("used")) / number(period["total"]) * 100
            seconds = {"5h": 18000, "five_hour": 18000, "weekly": 604800}.get(period.get("label"), 0)
            windows.append(window(key, used, period.get("reset_at"), seconds))
    if not any(windows):
        raise RequestError("invalid_response")
    return result(windows, str(item.get("tier") or ""))


def qoder_rpc(root, method):
    info = obj(read_json(root / "SharedClientCache/.info.json"))
    endpoint = info.get("ipcServerPath")
    if not isinstance(endpoint, str) or not endpoint:
        raise RequestError("unavailable")
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": {}}).encode()
    with LocalStream(endpoint, 2) as stream:
        stream.io(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
        data = bytearray()
        while True:
            data.extend(stream.io())
            if len(data) > 4 * 1024 * 1024:
                raise RequestError("response_too_large")
            header, sep, content = data.partition(b"\r\n\r\n")
            if not sep:
                continue
            size = re.search(rb"Content-Length:\s*(\d+)", header, re.I)
            if not size or int(size[1]) > 4 * 1024 * 1024:
                raise RequestError("invalid_response")
            if len(content) >= int(size[1]):
                payload = json.loads(content[:int(size[1])])
                if payload.get("error") or payload.get("id") != 1:
                    raise RequestError("invalid_response")
                return obj(payload.get("result"))


def qoder_rpc_body(body):
    quota = obj(first(body, "userQuota", "user_quota"))
    addon = obj(first(body, "addOnQuota", "add_on_quota"))
    if not quota and not addon:
        raise RequestError("invalid_response")
    def checked(raw):
        try:
            total, used = float(raw.get("total")), float(raw.get("used"))
            remaining = float(raw["remaining"]) if raw.get("remaining") is not None else max(0, total - used)
            if not all(math.isfinite(v) and v >= 0 for v in (total, used, remaining)) or total == 0 and (used != 0 or remaining != 0):
                raise ValueError()
            return total, used
        except (TypeError, ValueError):
            raise RequestError("invalid_response") from None
    total, amount = checked(quota) if quota else (0, 0)
    if addon:
        checked(addon)
    used = 0 if not total else (100 if first(body, "isQuotaExceeded", "is_quota_exceeded") and not addon else first(body, "totalUsagePercentage", default=quota.get("percentage")))
    if used is None or addon:
        used = amount / total * 100 if total else 0
    reset = first(body, "expiresAt", "expires_at")
    if timestamp(reset) >= 4102444800000:
        reset = None
    windows = [window("primary_window", used, reset) if quota else None,
               (fraction("tertiary_window", {"limit": addon.get("total"), "used": addon.get("used"), "remaining": addon.get("remaining")}) or window("tertiary_window", 0)) if addon else None]
    return result(windows, first(body, "userType", "user_type"))


def qoder_web_body(body):
    totals = []
    for key in (("totalQuota", "total_quota"), ("sharedQuota", "shared_quota")):
        container = obj(first(body, *key))
        summary = obj(first(container, "quotaSummary", "quota_summary", default=container))
        if summary:
            totals.append(summary)
    if not totals:
        raise RequestError("invalid_response")
    for raw in totals:
        if first(raw, "usedValue", "used_value") is None or first(raw, "limitValue", "limit_value") is None:
            raise RequestError("invalid_response")
    used = sum(number(first(v, "usedValue", "used_value")) for v in totals)
    total = sum(number(first(v, "limitValue", "limit_value")) for v in totals)
    percent = first(totals[0], "usagePercentage", "usage_percentage") if len(totals) == 1 else None
    if percent is None:
        percent = used / total * 100 if total else 100
    return result([window("primary_window", percent, first(body, "nextResetAt", "next_reset_at"))])


QODER_PUBLIC_KEY = b"""-----BEGIN PUBLIC KEY-----
MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDA8iMH5c02LilrsERw9t6Pv5Nc
4k6Pz1EaDicBMpdpxKduSZu5OANqUq8er4GM95omAGIOPOh+Nx0spthYA2BqGz+l
6HRkPJ7S236FZz73In/KVuLnwI8JJ2CbuJap8kvheCCZpmAWpb/cPx/3Vr/J6I17
XcW+ML9FoCI6AOvOzwIDAQAB
-----END PUBLIC KEY-----"""


def qoder_activity(q, auth, china):
    from cryptography.hazmat.primitives import padding, serialization
    from cryptography.hazmat.primitives.asymmetric import padding as asymmetric
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    user, token = auth.get("id") or auth.get("accountId"), auth.get("token")
    if not user or not token:
        return None
    temporary = uuid.uuid4().hex[:16].encode()
    key = base64.b64encode(serialization.load_pem_public_key(QODER_PUBLIC_KEY).encrypt(temporary, asymmetric.PKCS1v15())).decode()
    identity = {"name": auth.get("name", ""), "aid": str(user), "uid": str(user), "yx_uid": auth.get("yxUid", ""),
                "organization_id": auth.get("orgId", ""), "organization_name": auth.get("orgName", ""),
                "user_type": auth.get("userType") or "personal_standard", "security_oauth_token": token, "refresh_token": auth.get("refreshToken", "")}
    padder = padding.PKCS7(128).padder()
    padded = padder.update(json.dumps(identity, separators=(",", ":")).encode()) + padder.finalize()
    encryptor = Cipher(algorithms.AES(temporary), modes.CBC(temporary)).encryptor()
    info = base64.b64encode(encryptor.update(padded) + encryptor.finalize()).decode()
    payload = base64.b64encode(json.dumps({"version": "v1", "requestId": str(uuid.uuid4()), "info": info, "cosyVersion": "1.0.22", "ideVersion": ""}, separators=(",", ":")).encode()).decode()
    date = str(int(time.time()))
    signature = hashlib.md5((payload + "\n" + key + "\n" + date + "\n\n/api/v2/activity").encode()).hexdigest()
    machine = str(uuid.uuid4())
    headers = {"cosy-data-policy": "agree", "cosy-machinetype": "5", "cosy-clienttype": "5", "cosy-date": date,
               "cosy-user": str(user), "cosy-key": key, "cache-control": "no-cache", "cosy-business-product": "cli",
               "cosy-business-type": "agent", "cosy-scene": "assistant", "Authorization": "Bearer COSY." + payload + "." + signature,
               "cosy-version": "1.0.22", "cosy-machineid": machine, "cosy-machinetoken": machine, "login-version": "v2"}
    body = q.http(("https://openapi.qoder.com.cn" if china else "https://openapi.qoder.sh") + "/algo/api/v2/activity", headers=headers)
    for entry in array(obj(body.get("data")).get("activities")):
        if entry.get("activityId") == "ultimate_200_free_invoke" and entry.get("eligible") is not False and entry.get("tagStyle") != "EXPIRED":
            end = timestamp(entry.get("activityEndAt"))
            if end and end <= time.time() * 1000:
                continue
            return fraction("secondary_window", {"limit": entry.get("limit"), "used": entry.get("used"), "resetAt": entry.get("activityEndAt")})
    return None


def qoder_cn_cli(q):
    token = q.env.get("QODERCN_PERSONAL_ACCESS_TOKEN")
    org = None
    base = "https://openapi.qoder.com.cn"
    if token:
        body = q.http(base + "/api/v1/jobToken/exchange", body={"personal_token": token})
        body = obj(body.get("data") or body)
        token = first(body, "token", "device_token", "access_token")
        if not token:
            raise RequestError("reauth")
    else:
        root = q.root("QODERCN_CONFIG_DIR", ".qoder-cn") / ".auth"
        if not (root / "user").is_file():
            return None
        from cryptography.hazmat.primitives import padding
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        key = (root / "machine_id").read_text().strip()[:16].encode()
        if len(key) != 16:
            raise RequestError("reauth")
        decryptor = Cipher(algorithms.AES(key), modes.CBC(key)).decryptor()
        padded = decryptor.update(base64.b64decode((root / "user").read_bytes())) + decryptor.finalize()
        unpadder = padding.PKCS7(128).unpadder()
        auth = json.loads(unpadder.update(padded) + unpadder.finalize())
        token = first(auth, "security_oauth_token", "access_token")
        org = auth.get("organization_id")
    if not token:
        raise RequestError("reauth")
    body = q.http(base + ("/sash/api/v2/me/usage" if org else "/api/v2/quota/usage"), headers=bearer(token))
    body = obj(body.get("data") or body)
    return qoder_rpc_body(obj(body.get("qoderUsage")) if body.get("displayMode") == "qoder" else body)


def qoder(q, china=False):
    root = q.root("QODER_CN_HOME" if china else "QODER_HOME", str(q.root("APPDATA", "AppData/Roaming") / ("QoderCN" if china else "Qoder")))
    value, auth, error = None, {}, None
    try:
        value = qoder_rpc_body(qoder_rpc(root, "credit/usage"))
        try:
            auth = qoder_rpc(root, "auth/status")
        except RequestError:
            pass
    except (RequestError, OSError, ValueError) as exc:
        error = exc
    if not value and china:
        try:
            value = qoder_cn_cli(q)
        except (RequestError, OSError, ValueError) as exc:
            error = exc
    cookie = q.env.get("QODER_CN_COOKIE" if china else "QODER_COOKIE")
    if not value and cookie:
        origin = "https://qoder.cn" if china or q.env.get("QODER_SITE") == "china" else "https://qoder.com"
        value = qoder_web_body(q.http(origin + "/api/v2/me/usages/big_model_credits", headers={"Cookie": cookie, "Origin": origin, "Referer": origin + "/"}))
    if value:
        try:
            activity = qoder_activity(q, auth, china)
            if activity:
                value["windows"].append(activity)
        except (RequestError, ValueError, OSError):
            pass
        return value
    # 桌面已写出的额度快照为最后回退；只扫描近期有界日志，不读取会话正文。
    prefix = "QODER_CN" if china else "QODER"
    log_roots = [Path(q.env[prefix + "_LOG_ROOT"])] if q.env.get(prefix + "_LOG_ROOT") else [root / "logs", q.root("APPDATA", "AppData/Roaming") / ("com.qodercn.app.stable" if china else "com.qoder.app.stable") / "logs"]
    logs = sorted((p for folder in log_roots for name in ("main.log", "renderer.log") for p in folder.glob("**/" + name)), key=lambda p: p.stat().st_mtime, reverse=True)[:8]
    for path in logs:
        if time.time() - path.stat().st_mtime > 7 * 86400:
            continue
        with path.open("rb") as stream:
            stream.seek(max(0, path.stat().st_size - 8 * 1024 * 1024))
            text = stream.read().decode("utf-8", "replace")
        for line in reversed(text.splitlines()):
            if "userQuota" not in line and "totalQuota" not in line:
                continue
            start = line.find("{")
            if start < 0:
                continue
            try:
                body = json.loads(line[start:line.rfind("}") + 1])
                body = obj(body.get("qoderUsage")) or body
                value = qoder_rpc_body(body) if "userQuota" in body else qoder_web_body(body)
                value["stale"] = True
                stamp = re.match(r"\[([^\]]+)\]", line)
                observed = timestamp(stamp[1]) if stamp else path.stat().st_mtime * 1000
                if observed and time.time() * 1000 - observed > 7 * 86400 * 1000:
                    continue
                value["capturedAt"] = iso(observed or path.stat().st_mtime * 1000)
                return value
            except (ValueError, RequestError):
                pass
        matches = list(re.finditer(r"userType=([^,\s]+)[\s\S]*?isQuotaExceeded=(true|false)[\s\S]*?userQuota(?:=|\s+)used=([0-9.]+),\s*total=([0-9.]+),\s*remaining=([0-9.]+),\s*percentage=([0-9.]+),\s*unit=([^,\s]+)", text))
        if matches:
            match = matches[-1]
            value = result([window("primary_window", 0 if not number(match[4]) else 100 if match[2] == "true" else float(match[6]))], match[1])
            value["stale"], value["capturedAt"] = True, iso(path.stat().st_mtime * 1000)
            return value
    configured = root.is_dir() or bool(cookie) or china and (bool(q.env.get("QODERCN_PERSONAL_ACCESS_TOKEN")) or (q.root("QODERCN_CONFIG_DIR", ".qoder-cn") / ".auth/user").is_file())
    if configured and error:
        raise error if isinstance(error, RequestError) else RequestError("unavailable")
    return result(configured=configured, status="unavailable" if configured else "not_configured")


def zcode_decrypt(value, q):
    if not isinstance(value, str):
        return ""
    if not value.startswith("enc:v1:"):
        return value
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from cryptography.exceptions import InvalidTag
    parts = value[7:].split(".")
    if len(parts) != 3:
        return ""
    secret = q.env.get("ZCODE_CREDENTIAL_SECRET") or f"zcode-credential-fallback:{'win32' if os.name == 'nt' else 'linux'}:{q.home}:{getpass.getuser()}"
    try:
        iv, tag, encrypted = (b64(v) for v in parts)
        return AESGCM(hashlib.sha256(secret.encode()).digest()).decrypt(iv, encrypted + tag, None).decode()
    except (ValueError, UnicodeError, InvalidTag):
        return ""


def zcode_quota(body):
    if body.get("success") is False or body.get("code", 0) not in (0, 200):
        raise RequestError("invalid_response")
    data = obj(body.get("data"))
    limits = array(data.get("limits"))
    def matches(item, kind, unit, count=None):
        return item.get("type") == kind and number(item.get("unit")) == unit and (count is None or number(item.get("number")) == count)
    named = [next((v for v in limits if matches(v, "TOKENS_LIMIT", 3, 5)), None),
             next((v for v in limits if matches(v, "TOKENS_LIMIT", 6)), None),
             next((v for v in limits if matches(v, "TIME_LIMIT", 5, 1)), None)]
    chosen = [v for v in named if v] if any(named) else sorted(limits, key=lambda v: number(v.get("number")), reverse=True)
    windows = []
    for key, raw in zip(SLOTS, chosen):
        percent = raw.get("percentage")
        if percent is None and number(raw.get("number")):
            used = first(raw, "usage", "currentValue")
            if used is None and raw.get("remaining") is not None:
                used = max(0, number(raw["number"]) - number(raw["remaining"]))
            if used is not None:
                percent = number(used) / number(raw["number"]) * 100
        seconds = 18000 if matches(raw, "TOKENS_LIMIT", 3, 5) else 604800 if matches(raw, "TOKENS_LIMIT", 6, 1) else 0
        entry = window(key, percent, raw.get("nextResetTime"), seconds)
        if entry:
            entry["detail"] = str(raw.get("currentValue", "—")) + " / " + str(raw.get("usage", "—"))
        windows.append(entry)
    return result(windows, data.get("level") or "Coding")


def zcode_balance(body):
    data = obj(body.get("data"))
    balances = array(data.get("balances"))
    plans = array(data.get("plans"))
    periods = {ent.get("entitlement_id"): ent.get("period") for p in plans for ent in array(p.get("entitlements"))}
    balances.sort(key=lambda v: (periods.get(v.get("entitlement_id")) == "one_time", -number(v.get("priority")) if plans else 0, -number(v.get("total_units"))))
    return result([fraction(key, {"limit": raw.get("total_units"), "used": raw.get("used_units"), "resetAt": raw.get("period_end") or raw.get("expires_at")}) for key, raw in zip(SLOTS, balances)], balances[0].get("plan_id") if balances else "")


def zcode(q):
    root = q.root("ZCODE_HOME", ".zcode")
    config = obj(read_json(root / "v2/config.json"))
    setting = obj(read_json(root / "v2/setting.json"))
    credentials = obj(read_json(root / "v2/credentials.json"))
    decrypted = {k: zcode_decrypt(v, q) for k, v in credentials.items()}
    region = setting.get("providerFamilyDomain") or decrypted.get("oauth:active_provider")
    providers = obj(config.get("provider"))
    availability = obj(obj(obj(read_json(root / "v2/coding-plan-cache.json")).get("entryStatus")).get("items"))
    keys = ["builtin:" + family + "-" + plan for plan in ("coding-plan", "start-plan") for family in ((region, "bigmodel" if region == "zai" else "zai") if region in ("bigmodel", "zai") else ("bigmodel", "zai"))]
    candidates = []
    for key in keys:
        provider = obj(providers.get(key))
        if provider.get("enabled") is False or obj(availability.get(key)).get("status") not in (None, "available"):
            continue
        family = "bigmodel" if "bigmodel" in key else "zai"
        token = obj(provider.get("options")).get("apiKey")
        if token:
            selection = obj(setting.get("modelProviderFamilySelectedKeys")).get(family, "")
            match = re.fullmatch(r"team-plan:builtin:" + family + r"-coding-plan:[^:]+:([^:]+):([^:]+)", selection)
            team = (urllib.parse.unquote(match[1]), urllib.parse.unquote(match[2])) if match else None
            candidates.append((key, token, team))
        if "start-plan" in key and region == family and decrypted.get("zcodejwttoken"):
            candidates.append((key, decrypted["zcodejwttoken"], None))
        try:
            active_id = str(obj(json.loads(decrypted.get("oauth:" + family + ":user_info", "{}"))).get("id") or "")
        except ValueError:
            active_id = ""
        # 同一地区保留多个账号时，优先当前登录账号，避免展示旧账号额度。
        entries = sorted(decrypted.items(), key=lambda item: bool(active_id) and not item[0].endswith(":account:" + urllib.parse.quote(active_id, safe="") + ":api-key"))
        for name, value in entries:
            if (root / "v2/config.json").is_file():
                break  # 有配置时以当前启用 Provider 为准，不误用其他账号残留凭据。
            if not value or not name.startswith("account-provider:") or not name.endswith(":api-key") or family not in name:
                continue
            if "coding-plan" in key and "coding-plan" in name:
                team = None
                match = re.match(r"account-provider:team:([^:]+):([^:]+):([^:]+):([^:]+):account:[^:]+:api-key$", name)
                if match:
                    team = (urllib.parse.unquote(match[3]), urllib.parse.unquote(match[4]))
                candidates.append((key, value, team))
    error = None
    for key, token, team in candidates:
        try:
            if "coding-plan" in key:
                url = ("https://bigmodel.cn" if "bigmodel" in key else "https://api.z.ai") + "/api/monitor/usage/quota/limit"
                headers = {"authorization": token}
                if team:
                    headers.update({"bigmodel-organization": team[0], "bigmodel-project": team[1]})
                    url += "?type=2"
                value = zcode_quota(q.http(url, headers=headers))
            else:
                base = "https://zcode.z.ai/api/v1/zcode-plan"
                value = zcode_balance(q.http(base + "/billing/balance?app_version=3.2.5", headers={**bearer(token), "User-Agent": "ZCode/3.2.5", "X-ZCode-App-Version": "3.2.5", "X-Platform": "win32", "HTTP-Referer": "https://zcode.z.ai/"}))
            if value["windows"]:
                return value
        except RequestError as exc:
            error = exc
    # 与上游一致，在 API 临时不可达时读取最近六小时本机余额日志。
    for path in sorted((root / "v2/logs").glob("*.log"), key=lambda p: p.stat().st_mtime, reverse=True)[:2]:
        if time.time() - path.stat().st_mtime > 6 * 3600:
            continue
        with path.open("rb") as stream:
            stream.seek(max(0, path.stat().st_size - 2 * 1024 * 1024))
            text = stream.read().decode("utf-8", "replace")
        for line in reversed(text.splitlines()):
            if "[usage-stats] billing/balance 请求完成" not in line or "balances" not in line:
                continue
            start = line.find("{")
            if start < 0:
                continue
            try:
                payload = json.loads(line[start:line.rfind("}") + 1])
                if not payload.get("success") or payload.get("code") != 0 or obj(payload.get("payload")).get("code") != 0:
                    continue
                if any("coding-plan" in k for k, _, _ in candidates) and payload.get("providerId") not in {k for k, _, _ in candidates}:
                    continue
                stamp = re.match(r"\[([^\]]+)\]", line)
                observed = timestamp(stamp[1]) if stamp else 0
                if not observed or not 0 <= time.time() * 1000 - observed <= 6 * 3600 * 1000:
                    continue
                value = zcode_balance(payload["payload"])
                if value["windows"]:
                    value["stale"] = True
                    value["capturedAt"] = iso(observed)
                    return value
            except (ValueError, RequestError):
                pass
    if error:
        raise error
    return result(configured=bool(candidates))
