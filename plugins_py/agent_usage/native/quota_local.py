# quota_local.py — 本机 CLI、命名管道和桌面账号的额度查询
# 协议参考 TokenTracker 1.5.1（MIT），归属见 THIRD_PARTY_NOTICES.md。
from __future__ import annotations

import base64
import ctypes
from datetime import datetime, timedelta
import hashlib
import hmac
import http.client
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import ssl
import struct
import subprocess
import time

from .common import array, database, first, iso, number, obj, read_json, timestamp
from .network import RequestError, bearer, persist_credentials
from .quota import fraction, result, window


def command(args, timeout=10, env=None):
    try:
        run = subprocess.run(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, timeout=timeout, env=env,
                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        text = (run.stdout + b"\n" + run.stderr).decode("utf-8", "replace")
        if len(text) > 4 * 1024 * 1024:
            raise RequestError("response_too_large")
        return text, run.returncode
    except (OSError, subprocess.TimeoutExpired):
        raise RequestError("unavailable") from None


class LocalStream:
    """有期限的本机字节流；Windows 只允许本机命名管道。"""
    def __init__(self, endpoint, timeout=8):
        self.deadline = time.monotonic() + timeout
        self.buffer = bytearray()
        self.handle = None
        self.sock = None
        if os.name != "nt":
            if not Path(endpoint).is_absolute():
                raise RequestError("invalid_endpoint")
            self.sock = socket.socket(socket.AF_UNIX)
            self.sock.settimeout(timeout)
            self.sock.connect(endpoint)
            return
        if not re.fullmatch(r"\\\\\.\\pipe\\[^\\\r\n]+", endpoint):
            raise RequestError("invalid_endpoint")
        from ctypes import wintypes as w
        self.k = ctypes.WinDLL("kernel32", use_last_error=True)
        self.k.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, ctypes.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
        self.k.CreateFileW.restype = w.HANDLE
        self.k.CloseHandle.argtypes = [w.HANDLE]
        self.k.CreateEventW.argtypes = [ctypes.c_void_p, w.BOOL, w.BOOL, w.LPCWSTR]
        self.k.CreateEventW.restype = w.HANDLE
        self.k.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
        self.k.CancelIoEx.argtypes = [w.HANDLE, ctypes.c_void_p]
        self.k.GetOverlappedResult.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.POINTER(w.DWORD), w.BOOL]
        self.k.ReadFile.argtypes = self.k.WriteFile.argtypes = [w.HANDLE, ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD), ctypes.c_void_p]
        class Overlapped(ctypes.Structure):
            _fields_ = [("Internal", ctypes.c_size_t), ("InternalHigh", ctypes.c_size_t),
                        ("Offset", w.DWORD), ("OffsetHigh", w.DWORD), ("hEvent", w.HANDLE)]
        self.Overlapped = Overlapped
        self.handle = self.k.CreateFileW(endpoint, 0xC0000000, 0, None, 3, 0x40000000, None)
        if self.handle == ctypes.c_void_p(-1).value:
            self.handle = None
            raise RequestError("unavailable")

    def io(self, data=None):
        remain = self.deadline - time.monotonic()
        if remain <= 0:
            raise RequestError("timeout")
        if self.sock:
            self.sock.settimeout(remain)
            if data is not None:
                self.sock.sendall(data)
                return b""
            raw = self.sock.recv(65536)
            if not raw:
                raise RequestError("unavailable")
            return raw
        from ctypes import wintypes as w
        event = self.k.CreateEventW(None, True, False, None)
        overlap = self.Overlapped(hEvent=event)
        buffer = ctypes.create_string_buffer(data) if data is not None else ctypes.create_string_buffer(65536)
        count = w.DWORD()
        try:
            operation = self.k.WriteFile if data is not None else self.k.ReadFile
            ok = operation(self.handle, buffer, len(data) if data is not None else 65536, ctypes.byref(count), ctypes.byref(overlap))
            error = ctypes.get_last_error()
            if not ok and error != 997:
                raise RequestError("unavailable")
            if not ok:
                if self.k.WaitForSingleObject(event, max(1, int(remain * 1000))) != 0:
                    self.k.CancelIoEx(self.handle, ctypes.byref(overlap))
                    self.k.GetOverlappedResult(self.handle, ctypes.byref(overlap), ctypes.byref(count), True)
                    raise RequestError("timeout")
                if not self.k.GetOverlappedResult(self.handle, ctypes.byref(overlap), ctypes.byref(count), False):
                    raise RequestError("unavailable")
            if data is not None:
                if count.value != len(data):
                    raise RequestError("unavailable")
                return b""
            if not count.value:
                raise RequestError("unavailable")
            return buffer.raw[:count.value]
        finally:
            self.k.CloseHandle(event)

    def send_json(self, payload):
        self.io(json.dumps(payload, separators=(",", ":")).encode() + b"\n")

    def line_json(self):
        while b"\n" not in self.buffer:
            self.buffer.extend(self.io())
            if len(self.buffer) > 1024 * 1024:
                raise RequestError("response_too_large")
        line, _, remaining = self.buffer.partition(b"\n")
        self.buffer[:] = remaining
        return json.loads(line)

    def close(self):
        if self.handle:
            self.k.CloseHandle(self.handle)
            self.handle = None
        if self.sock:
            self.sock.close()
            self.sock = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def wb_proof(ticket, role, endpoint, client, server):
    fields = ("wbipc-s" if role == "server" else "wbipc-c", "1", endpoint, client, server)
    raw = b"".join(struct.pack(">I", len(v.encode())) + v.encode() for v in fields)
    return base64.urlsafe_b64encode(hmac.new(ticket.encode(), raw, "sha256").digest()).decode().rstrip("=")


class WorkbuddyIpc:
    def __init__(self, root):
        path = Path(root) / "wbipc/endpoint.json"
        if path.is_symlink() or path.stat().st_size > 16384:
            raise RequestError("invalid_endpoint")
        discovery = obj(read_json(path))
        endpoint, ticket = discovery.get("endpoint", ""), discovery.get("ticket", "")
        if not re.fullmatch(r"[\w-]{43}", ticket, re.ASCII):
            raise RequestError("invalid_endpoint")
        if os.name == "nt" and not re.fullmatch(r"\\\\\.\\pipe\\wbipc-[\w-]+", endpoint, re.ASCII):
            raise RequestError("invalid_endpoint")
        if os.name != "nt":
            stat = path.stat()
            socket_stat = Path(endpoint).lstat()
            if stat.st_uid != os.getuid() or stat.st_mode & 0o077 or socket_stat.st_uid != os.getuid() or socket_stat.st_mode & 0o077:
                raise RequestError("invalid_endpoint")
        self.stream = LocalStream(endpoint, 15)
        self.next_id = 1
        client = secrets.token_urlsafe(16)
        try:
            self.stream.send_json({"type": "session_hello", "protocol_min": 1, "protocol_max": 1,
                                   "client_nonce": client, "ticket_id": hashlib.sha256(ticket.encode()).hexdigest()[:16],
                                   "client": {"kind": "xuanhub", "id": "subscription-quota", "version": "1"}})
            challenge = self.stream.line_json()
            server = challenge.get("server_nonce", "")
            if challenge.get("type") != "session_challenge" or challenge.get("protocol") != 1 or not re.fullmatch(r"[\w-]{16,128}", server, re.ASCII):
                raise RequestError("invalid_response")
            if not hmac.compare_digest(challenge.get("server_proof", ""), wb_proof(ticket, "server", endpoint, client, server)):
                raise RequestError("reauth")
            self.stream.send_json({"type": "session_prove", "client_proof": wb_proof(ticket, "client", endpoint, client, server)})
            ack = self.stream.line_json()
            if ack.get("type") != "session_hello_ack" or ack.get("protocol") != 1 or "wb.request" not in array(ack.get("pipes")):
                raise RequestError("invalid_response")
            pipe = self.rpc("broker/GetPipe", {"pipe": "wb.request"})
            if pipe.get("channel") != "c:wb.request" or "http.fetch" not in array(pipe.get("methods")):
                raise RequestError("invalid_response")
        except Exception:
            self.close()
            raise

    def rpc(self, method, params, mode=None):
        request_id = self.next_id
        self.next_id += 1
        self.stream.send_json({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params,
                               **({"mode": mode} if mode else {})})
        packet = self.stream.line_json()
        if packet.get("id") != request_id or packet.get("jsonrpc") != "2.0" or packet.get("error"):
            raise RequestError("invalid_response")
        return obj(packet.get("result"))

    def post(self, path, body):
        if path not in {"/billing/meter/get-user-resource-" + p for p in ("summary", "paid-packages", "free-packages")}:
            raise RequestError("invalid_endpoint")
        response = self.rpc("c:wb.request/http.fetch", {"method": "POST", "path": path,
            "headers": {"content-type": "application/json", "accept": "application/json"},
            "body_b64": base64.b64encode(json.dumps(body).encode()).decode()}, "call")
        if response.get("status") in (401, 403):
            raise RequestError("reauth")
        if response.get("status") != 200:
            raise RequestError("unavailable")
        return json.loads(base64.b64decode(response["body_b64"], validate=True))

    def close(self):
        self.stream.close()


WB_PLANS = {
    "TCACA_code_027_0FCGVA6vSa": (4, "Ultimate"), "TCACA_code_026_BaESVICNoi": (3, "Advanced"),
    "TCACA_code_002_AkiJS3ZHF5": (2, "STANDARD"), "TCACA_code_005_maRGyrHhw1": (2, "STANDARD"),
    "TCACA_code_003_FAnt7lcmRT": (2, "STANDARD"), "TCACA_code_023_4xbGhMrE6q": (1, "Youth"),
    "TCACA_code_035_ArVxJcGDsm": (0, "Free"), "TCACA_code_039_KRcQj7wUat": (0, "STANDARD TRIAL"),
    "TCACA_code_040_mi9rCYg46x": (0, "STANDARD TRIAL"), "TCACA_code_008_cfWoLwvjU4": (0, "Free"),
}
WB_REWARDS = {"TCACA_code_" + code for code in (
    "001_PqouKr6QWV", "006_DbXS0lrypC", "007_nzdH5h4Nl0", "009_0XmEQc2xOf", "028_NtpWi0jzXs",
    "029_6wCGEWquYy", "030_BjSt89qTvr", "036_lupO5WgNdG", "037_WxOD3MpI2o", "038_OhvqZtiPKr")}


def billing_body(payload):
    if first(payload, "code", "Code", default=0) not in (0, "0"):
        raise RequestError("unavailable")
    return first(payload, "data", "Data", default=payload)


def workbuddy_plan(post, international=False):
    prefix = "/billing/meter/get-user-resource-"
    summary = billing_body(post(prefix + "summary", {}))
    identity = first(obj(summary), "SubscriptionPackageCode", "subscriptionPackageCode", default="")
    rows = []
    def collect(data):
        if isinstance(data, list):
            rows.extend(data)
        else:
            for key in ("Packages", "packages", "Accounts", "accounts", "Resources", "resources"):
                rows.extend(array(obj(data).get(key)))
    collect(summary)
    codes = list(WB_PLANS)
    if identity and identity not in WB_REWARDS and identity not in codes:
        codes.append(identity)
    for kind in ("paid", "free"):
        for page in range(1, 11):
            data = billing_body(post(prefix + kind + "-packages", {"PageNumber": page, "PageSize": 100,
                    "PackageCodes": codes, "Status": [0, 3], **({"NeedRenewInfo": True} if kind == "paid" else {})}))
            collect(data)
            if len(array(obj(data).get("Accounts"))) < 100 or 0 < number(obj(data).get("TotalCount")) <= page * 100:
                break
        else:
            raise RequestError("incomplete_response")
    merged = {}
    for row in rows:
        code = first(obj(row), "PackageCode", "packageCode", "package_code", default="")
        if code in WB_REWARDS or (code not in WB_PLANS and code != identity):
            continue
        values = {"total": first(row, "CycleTotalCapacity", "cycleTotal", "CycleCapacitySizePrecise", "total"),
                  "remaining": first(row, "CycleRemainCapacity", "cycleRemain", "CycleCapacityRemainPrecise", "left"),
                  "used": first(row, "CycleUsedCapacity", "cycleUsed", "used"),
                  "resetAt": first(row, "CycleEndTime", "cycleEnd", "cycle_end", "DeductionEndTime", "expireAt"),
                  "name": first(row, "PackageName", "packageName", "name")}
        merged[code] = {**merged.get(code, {}), **{k: v for k, v in values.items() if v is not None}}
    if not merged:
        return result()
    code, chosen = max(merged.items(), key=lambda pair: WB_PLANS.get(pair[0], (0, ""))[0] * 1e9 +
                       (1e6 if number(pair[1].get("total")) > 0 and number(pair[1].get("remaining")) > 0 else 0) + number(pair[1].get("total")))
    name = first(obj(summary), "SubscriptionPackageName", "subscriptionPackageName") or chosen.get("name")
    label = name if isinstance(name, str) and name.isascii() and re.search("[A-Za-z]", name) and not name.startswith("TCACA_code_") else WB_PLANS.get(code, (0, ""))[1]
    if international:
        label = {"STANDARD": "Pro", "STANDARD TRIAL": "Pro-Trial"}.get(label, label)
    return result([fraction("primary_window", chosen)], label)


def workbuddy_variant(q, international=False):
    from urllib.parse import urlsplit
    variable = "WORKBUDDY_AI_HOME" if international else "WORKBUDDY_HOME"
    default = ".workbuddy-ai" if international else ".workbuddy"
    root = q.root(variable, default)
    mode = q.env.get("TOKENTRACKER_WSL_MODE", "both").lower()
    if os.name == "nt" and not q.env.get(variable) and mode != "native-only" and (not root.is_dir() or mode == "wsl-only"):
        from .discovery import running_wsl_homes
        if not hasattr(q, "_workbuddy_wsl_homes"):
            q._workbuddy_wsl_homes = running_wsl_homes(q.env)
        for home in q._workbuddy_wsl_homes:
            try:
                if (home / default).is_dir():
                    root = home / default
                    break
            except OSError:
                continue
    suffixes = ("workbuddy.ai",) if international else ("workbuddy.cn", "copilot.tencent.com", "wb.tencentbuddy.com")
    origin = "https://www.workbuddy.ai" if international else "https://www.workbuddy.cn"
    def allowed(value):
        parsed = urlsplit(value if "://" in value else "https://" + value)
        return parsed.scheme == "https" and any(parsed.hostname == s or (parsed.hostname or "").endswith("." + s) for s in suffixes)
    for path in list((root / "storage").glob("**/wb-product-config.json"))[:5]:
        endpoint = obj(obj(obj(read_json(path)).get("product-config-snapshot")).get("data")).get("endpoint")
        if endpoint:
            if not allowed(endpoint):
                raise RequestError("invalid_endpoint")
            origin = endpoint.rstrip("/")
            break
    if (root / "wbipc/endpoint.json").is_file():
        client = WorkbuddyIpc(root)
        try:
            return workbuddy_plan(client.post, international)
        finally:
            client.close()
    token = q.env.get("WORKBUDDY_AI_ACCESS_TOKEN" if international else "WORKBUDDY_ACCESS_TOKEN")
    encrypted = False
    auth_root = root.parent / ".config" if str(root).startswith(("\\\\wsl.localhost\\", "\\\\wsl$\\")) else q.root("APPDATA", "AppData/Roaming")
    for folder in (auth_root / "CodeBuddyExtension/Data/Public/auth", root / "auth"):
        for name in (("workbuddy-ai.info", "workbuddy-desktop.info") if international else ("workbuddy-desktop.info",)):
            auth = obj(obj(read_json(folder / name)).get("auth"))
            if token or not allowed(str(auth.get("domain") or "")):
                continue
            if isinstance(auth.get("accessToken"), str):
                token = auth["accessToken"]
            elif obj(auth.get("accessToken")).get("$wbEncrypted") == 1:
                encrypted = True
    if not token:
        return result(configured=root.is_dir() or encrypted, status="unavailable" if root.is_dir() or encrypted else "not_configured")
    return workbuddy_plan(lambda path, body: q.http(origin + path, headers=bearer(token), body=body), international)


def workbuddy(q):
    values = []
    for international in (False, True):
        try:
            values.append(workbuddy_variant(q, international))
        except RequestError as error:
            values.append(result(status="reauth" if str(error) == "reauth" else "unavailable"))
    return next((v for v in values if v["windows"]), next((v for v in values if v["configured"]), result(configured=False)))


def kiro(q):
    binary = shutil.which("kiro-cli", path=q.env.get("PATH", ""))
    if not binary:
        return result(configured=False)
    raw, _ = command([binary, "chat", "--no-interactive", "/usage"], 20, {**q.env, "TERM": "xterm-256color"})
    text = re.sub(r"\x1b\[[0-9;?]*[ -/]*[@-~]", "", raw)
    if re.search(r"not logged in|login required|kiro-cli login|oauth error", text, re.I):
        raise RequestError("reauth")
    plan = re.search(r"Plan:\s*([^\n]+)|\|\s*(KIRO\s+\w+)", text)
    covered = re.search(r"\(([\d.]+)\s+of\s+([\d.]+)\s+covered", text, re.I)
    percent = re.search(r"(?:█|�)+\s*(\d+)%", text)
    used = number(percent[1]) if percent else (number(covered[1]) / number(covered[2]) * 100 if covered and number(covered[2]) else None)
    if used is None and re.search(r"managed by admin|managed by organization", text, re.I):
        used = 0
    if used is None:
        raise RequestError("invalid_response")
    reset = re.search(r"resets on (\d{4}-\d{2}-\d{2}|\d{2}/\d{2})", text, re.I)
    date = None
    if reset:
        try:
            date = datetime.strptime(reset[1], "%Y-%m-%d" if "-" in reset[1] else "%m/%d").replace(tzinfo=datetime.now().astimezone().tzinfo)
            if "/" in reset[1]:
                date = date.replace(year=datetime.now().year)
                if date < datetime.now().astimezone() - timedelta(days=1):
                    date = date.replace(year=date.year + 1)
        except ValueError:
            pass
    bonus = re.search(r"Bonus credits:[\s\S]*?([\d.]+)/([\d.]+)", text, re.I)
    expiry = re.search(r"expires in (\d+) days?", text, re.I)
    return result([window("primary_window", used, date.isoformat() if date else None),
                   window("secondary_window", number(bonus[1]) / number(bonus[2]) * 100 if bonus and number(bonus[2]) else None,
                          (time.time() + int(expiry[1]) * 86400) if expiry else None)], (plan[1] or plan[2]).strip() if plan else "Kiro")


def copilot(q):
    root = q.home / ".config/github-copilot"
    token = None
    for file in ("apps.json", "hosts.json"):
        entries = obj(read_json(root / file))
        entries = sorted(entries.items(), key=lambda pair: not pair[0].startswith("github.com"))
        token = next((obj(v).get("oauth_token") for _, v in entries if obj(v).get("oauth_token")), token)
        if token:
            break
    if not token and (root / "auth.db").is_file():
        with database(root / "auth.db") as db:
            rows = db.execute("SELECT token_schema_version, token_ciphertext, auth_authority FROM oauth_tokens ORDER BY last_used_at DESC, updated_at DESC").fetchall()
        for row in sorted(rows, key=lambda r: "github.com" not in str(r[2])):
            if int(row[0] or 0) != 0:
                continue  # 上游在 Windows 同样不解密 macOS 钥匙串版本。
            candidate = row[1].decode("utf-8", "replace") if isinstance(row[1], bytes) else str(row[1] or "")
            if re.match(r"(?:gh[opusr]_|github_pat_)", candidate):
                token = candidate.strip()
                break
    if not token:
        return result(configured=False)
    body = q.http("https://api.github.com/copilot_internal/user", headers={"Authorization": "token " + token,
        "Editor-Version": "vscode/1.96.2", "Editor-Plugin-Version": "copilot-chat/0.26.7", "User-Agent": "GitHubCopilotChat/0.26.7", "X-Github-Api-Version": "2025-04-01"})
    windows = []
    for name, key in (("premium_interactions", "primary_window"), ("chat", "secondary_window")):
        value = obj(obj(body.get("quota_snapshots")).get(name))
        if not any(number(value.get(k)) for k in ("entitlement", "remaining", "percent_remaining")):
            continue
        used = 100 - number(value["percent_remaining"]) if value.get("percent_remaining") is not None else ((number(value.get("entitlement")) - number(value.get("remaining"))) / number(value["entitlement"]) * 100 if number(value.get("entitlement")) else None)
        windows.append(window(key, used, body.get("quota_reset_date")))
    return result(windows, body.get("copilot_plan"))


def antigravity_summary(body):
    windows = []
    slots = {"3p-weekly": "primary_window", "3p-5h": "secondary_window", "gemini-weekly": "tertiary_window", "gemini-5h": "quaternary_window"}
    for group in array(obj(body.get("response", body)).get("groups")):
        for bucket in array(obj(group).get("buckets")):
            if bucket.get("bucketId") in slots and bucket.get("remainingFraction") is not None:
                reset = bucket.get("resetTime")
                if isinstance(reset, dict):
                    reset = reset.get("seconds")
                windows.append(window(slots[bucket["bucketId"]], round((1 - number(bucket["remainingFraction"])) * 100), reset))
    if not windows:
        raise RequestError("invalid_response")
    return result(windows)


def antigravity_models(body):
    user = obj(body.get("userStatus"))
    models = array(obj(user.get("cascadeModelConfigData")).get("clientModelConfigs")) or array(body.get("clientModelConfigs"))
    families = {"claude": [], "gemini": []}
    all_values = []
    for model in models:
        quota = obj(model.get("quotaInfo"))
        if quota.get("remainingFraction") is None:
            continue
        name = str(model.get("label", "")) + str(obj(model.get("modelOrAlias")).get("model", ""))
        family = "claude" if "claude" in name.lower() else "gemini" if "gemini" in name.lower() else None
        reset = quota.get("resetTime")
        entry = (number(quota["remainingFraction"]), reset.get("seconds") if isinstance(reset, dict) else reset)
        all_values.append(entry)
        if family:
            families[family].append(entry)
    windows = []
    for family, slots in (("claude", ("primary_window", "secondary_window")), ("gemini", ("tertiary_window", "quaternary_window"))):
        values = sorted(families[family], key=lambda pair: pair[0])
        if values:
            for slot, value in zip(slots, (values[0], values[-1])):
                windows.append(window(slot, 100 - value[0] * 100, value[1]))
        elif all_values:
            value = min(all_values, key=lambda pair: pair[0])
            windows.append(window(slots[0], 100 - value[0] * 100, value[1]))
    if not windows:
        raise RequestError("invalid_response")
    plan = obj(obj(user.get("planStatus")).get("planInfo"))
    return result(windows, first(plan, "planDisplayName", "displayName", "productName", "planName", "planShortName"))


def local_json(port, path, body, csrf, scheme="https"):
    if not isinstance(port, int) or not 1 <= port <= 65535 or not path.startswith("/exa.language_server_pb.LanguageServerService/"):
        raise RequestError("invalid_endpoint")
    # 自签名证书例外严格限制到当前进程发现的 127.0.0.1 端口。
    connection = http.client.HTTPSConnection("127.0.0.1", port, timeout=3, context=ssl._create_unverified_context()) if scheme == "https" else http.client.HTTPConnection("127.0.0.1", port, timeout=3)
    try:
        connection.request("POST", path, json.dumps(body), {"Content-Type": "application/json", "Connect-Protocol-Version": "1", **({"X-Codeium-Csrf-Token": csrf} if csrf else {})})
        response = connection.getresponse()
        raw = response.read(4 * 1024 * 1024 + 1)
        if response.status != 200 or len(raw) > 4 * 1024 * 1024:
            raise RequestError("unavailable")
        return json.loads(raw)
    finally:
        connection.close()


def antigravity(q):
    candidates = []
    for relative in ("jetski-standalone-oauth-token", "antigravity/jetski-standalone-oauth-token", "antigravity/antigravity-oauth-token", "antigravity-ide/antigravity-oauth-token", "antigravity-cli/antigravity-oauth-token"):
        path = q.home / ".gemini" / relative
        try:
            raw = path.read_text(encoding="utf-8").strip()
            if raw.startswith("go-keyring-base64:"):
                raw = base64.b64decode(raw.split(":", 1)[1]).decode()
            auth = obj(json.loads(raw))
            token = obj(auth.get("token")) or auth
            if token.get("access_token") or isinstance(auth.get("token"), str):
                candidates.append((path, auth, token))
        except (OSError, ValueError, UnicodeError):
            continue
    candidates.sort(key=lambda entry: timestamp(first(entry[2], "expiry", "expiry_date")), reverse=True)
    remote_error = None
    if candidates:
        path, original, auth = candidates[0]
        access = auth.get("access_token") or original.get("token")
        def refresh():
            nonlocal original, auth, access
            fresh = q.http("https://oauth2.googleapis.com/token", form={
                "client_id": "1071006060591-tmhssin2h21lcre235vtolojh4g403ep.apps.googleusercontent.com",
                "client_secret": "GOCSPX-" + "K58FWR48" + "6LdLJ1mL" + "B8sXC4z6qDAf",
                "grant_type": "refresh_token", "refresh_token": auth["refresh_token"]})
            if not fresh.get("access_token"):
                raise RequestError("reauth")
            access = fresh["access_token"]
            updated_token = {**auth, "access_token": access, "refresh_token": fresh.get("refresh_token") or auth["refresh_token"], "expiry": iso(time.time() * 1000 + number(fresh.get("expires_in")) * 1000)}
            updated = {**original, "token": updated_token} if isinstance(original.get("token"), dict) else updated_token
            if read_json(path) == original:
                persist_credentials(path, original, updated)
            original, auth = updated, updated_token
        try:
            expiry = timestamp(first(auth, "expiry", "expiry_date"))
            if expiry and expiry <= time.time() * 1000 + 60000 and auth.get("refresh_token"):
                refresh()
            retried = False
            for host in ("daily-cloudcode-pa.googleapis.com", "cloudcode-pa.googleapis.com"):
                try:
                    try:
                        payload = q.http("https://" + host + "/v1internal:retrieveUserQuotaSummary", headers={**bearer(access), "User-Agent": "antigravity/2.9.1"}, body={})
                    except RequestError as error:
                        if str(error) != "reauth" or retried or not auth.get("refresh_token"):
                            raise
                        retried = True
                        refresh()
                        payload = q.http("https://" + host + "/v1internal:retrieveUserQuotaSummary", headers={**bearer(access), "User-Agent": "antigravity/2.9.1"}, body={})
                    value = antigravity_summary(payload)
                    try:
                        plan = q.http("https://daily-cloudcode-pa.googleapis.com/v1internal:loadCodeAssist", headers=bearer(access), body={"metadata": {"ideType": "ANTIGRAVITY"}})
                        value["plan"] = str(first(obj(plan.get("paidTier")), "name", "id", default=""))[:100]
                    except RequestError:
                        pass
                    return value
                except RequestError as error:
                    remote_error = error
        except RequestError as error:
            remote_error = error
    if os.name != "nt":
        if remote_error:
            raise remote_error
        return result(configured=False)
    script = "$ErrorActionPreference='Stop'; Get-CimInstance Win32_Process -Filter \"Name LIKE '%language_server%'\" | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress"
    raw, code = command(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script], 4)
    try:
        processes = json.loads(raw.strip() or "[]")
    except ValueError:
        processes = []
    if isinstance(processes, dict):
        processes = [processes]
    process = next((p for p in processes if "antigravity" in str(p.get("CommandLine", "")).lower() or "jetski" in str(p.get("CommandLine", "")).lower()), None)
    if not process:
        if remote_error:
            raise remote_error
        return result(configured=bool(candidates), status="unavailable" if candidates else "not_configured")
    flags = {}
    for name in ("csrf_token", "extension_server_port"):
        match = re.search(r"--" + name + r"(?:=|\s+)(?:\"([^\"]+)\"|(\S+))", str(process.get("CommandLine", "")))
        flags[name] = (match[1] or match[2]) if match else ""
    raw, _ = command(["netstat.exe", "-ano", "-p", "tcp"], 4)
    ports = []
    for line in raw.splitlines():
        parts = line.split()
        if len(parts) >= 5 and parts[-1] == str(process["ProcessId"]) and parts[-2] == "LISTENING":
            try:
                ports.append(int(parts[1].rsplit(":", 1)[1]))
            except ValueError:
                pass
    prefix = "/exa.language_server_pb.LanguageServerService/"
    body = {"metadata": {"ideName": "antigravity", "extensionName": "antigravity", "ideVersion": "unknown", "locale": "en"}}
    for port in list(dict.fromkeys(ports))[:8]:
        for scheme in (("https",) if flags["csrf_token"] else ("https", "http")):
            for method, normalize in (("RetrieveUserQuotaSummary", antigravity_summary), ("GetUserStatus", antigravity_models), ("GetCommandModelConfigs", antigravity_models)):
                try:
                    # IDE 扩展服务可能不归 language_server 的 PID 持有端口。
                    target = int(flags["extension_server_port"]) if method == "GetCommandModelConfigs" and flags["extension_server_port"].isdigit() else port
                    target_scheme = "http" if target != port else scheme
                    return normalize(local_json(target, prefix + method, body, flags["csrf_token"], target_scheme))
                except (RequestError, OSError, ValueError, http.client.HTTPException):
                    continue
    raise remote_error or RequestError("unavailable")
