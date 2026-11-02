# account_usage.py — Cursor 与 TRAE CN 官方账号用量的原生读取
from __future__ import annotations

import base64
import csv
from collections import Counter
import hashlib
import hmac
import io
import json
import os
from pathlib import Path
import re
import sys
import time
import urllib.parse
import urllib.request

from .common import database, event, integer, obj, read_json, totals
from .network import Http, RequestError, build_opener, jwt

CURSOR_CSV_URL = "https://cursor.com/api/dashboard/export-usage-events-csv?strategy=tokens"
TRAE_CN_USAGE_URL = "https://api.trae.cn/trae/api/v1/pay/query_user_usage_group_by_session"


class _CursorRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        target = urllib.parse.urlsplit(newurl)
        if (target.scheme != "https" or target.hostname not in {"cursor.com", "www.cursor.com"}
                or target.port not in {None, 443} or target.username or target.password
                or getattr(req, "_cursor_redirected", False)):
            raise RequestError("redirect_rejected", code)
        redirected = super().redirect_request(req, fp, code, msg, headers, newurl)
        if redirected is not None:
            redirected._cursor_redirected = True
        return redirected


class CursorHttp(Http):
    def __init__(self, timeout=30):
        super().__init__(timeout)
        # Cursor 的 CSV 端点会在两个官方主机间迁移；仅允许一次同服务跳转。
        self.opener = build_opener(_CursorRedirect())


def cursor_subject(value):
    if not isinstance(value, str):
        return None
    native = re.search(r"\|(user_[A-Za-z0-9_]+)$", value)
    if native:
        return native[1]
    return value if re.fullmatch(r"(?:google-oauth2|github|oidc|auth0)\|[^|]+", value) else None


def cursor_credentials(home, env=None):
    home, env = Path(home), dict(os.environ if env is None else env)
    app = Path(env.get("APPDATA") or home / "AppData/Roaming") if sys.platform == "win32" else (home / "Library/Application Support" if sys.platform == "darwin" else Path(env.get("XDG_CONFIG_HOME") or home / ".config"))
    path = app / "Cursor/User/globalStorage/state.vscdb"
    if not path.is_file():
        return None
    with database(path) as db:
        row = db.execute("SELECT value FROM ItemTable WHERE key='cursorAuth/accessToken'").fetchone()
    if not row or not isinstance(row[0], str) or len(row[0]) < 10:
        return None
    token = row[0]
    config = obj(read_json(home / ".cursor/cli-config.json", {}))
    user = cursor_subject(obj(config.get("authInfo")).get("authId")) or cursor_subject(jwt(token).get("sub"))
    if not user:
        return None
    return {"cookie": f"WorkosCursorSessionToken={user}%3A%3A{token}", "accessToken": token}


def cursor_auth(home, env=None):
    auth = cursor_credentials(home, env)
    return {"Cookie": auth["cookie"]} if auth else None


def parse_cursor_csv(payload):
    text = payload.decode("utf-8-sig") if isinstance(payload, bytes) else payload.lstrip("\ufeff")
    reader = csv.DictReader(io.StringIO(text))
    required = {"Date", "Model", "Input (w/ Cache Write)", "Input (w/o Cache Write)", "Cache Read", "Output Tokens", "Total Tokens"}
    if not reader.fieldnames or not required.issubset(reader.fieldnames):
        raise RequestError("invalid_response")
    # 同一时间模型允许出现完全相同的多次请求；序号保留重数，重复整份导出仍幂等。
    counts = Counter()
    result = []
    for row in reader:
        def count(name):
            return integer(str(row.get(name) or "0").replace(",", ""))
        usage = totals(count("Input (w/o Cache Write)"), count("Output Tokens"), count("Cache Read"),
                       max(0, count("Input (w/ Cache Write)") - count("Input (w/o Cache Write)")))
        fingerprint = (row.get("Date"), row.get("Model"), row.get("Kind"), row.get("Max Mode"), *usage.values())
        counts[fingerprint] += 1
        item = event("cursor", row.get("Model"), row.get("Date"), usage,
                     (fingerprint, counts[fingerprint]), source_scope="account")
        if item:
            result.append(item)
    return result


def cursor_usage(home, env=None, http=None):
    auth = cursor_auth(home, env)
    if not auth:
        return []
    return parse_cursor_csv((http or CursorHttp())(CURSOR_CSV_URL, headers={**auth, "Referer": "https://www.cursor.com/settings"}, raw=True))


# TRAE 客户端 tc-v5 格式的公开常量；这不是用户口令或登录令牌。
_JG = bytes([82,9,106,213,48,54,165,56,191,64,163,158,129,243,215,251,124,227,57,130,155,47,255,135,52,142,67,68,196,222,233,203,84,123,148,50,166,194,35,61,238,76,149,11,66,250,195,78,8,46,161,102,40,217,36,178,118,91,162,73,109,139,209,37])
_KG = bytes([31,221,168,51,136,7,199,49,177,18,16,89,39,128,236,95,96,81,127,169,25,181,74,13,45,229,122,159,147,201,156,239,160,224,59,77,174,42,245,176,200,235,187,60,131,83,153,97,23,43,4,126,186,119,214,38,225,105,20,99,85,33,12,125])


def decrypt_trae_cn(blob):
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.primitives.padding import PKCS7
    try:
        if len(blob) < 54 or blob[:6] != b"tc\x05\x10\x00\x00" or (len(blob) - 38) % 16:
            raise ValueError()
        key = hashlib.sha512(hashlib.sha512(blob[6:38]).digest() + bytes(a ^ b for a, b in zip(_JG, _KG))).digest()
        decryptor = Cipher(algorithms.AES(key[:16]), modes.CBC(key[16:32])).decryptor()
        unpadder = PKCS7(128).unpadder()
        padded = decryptor.update(blob[38:]) + decryptor.finalize()
        plain = unpadder.update(padded) + unpadder.finalize()
        if len(plain) < 64 or not hmac.compare_digest(plain[:64], hashlib.sha512(plain[64:]).digest()):
            raise ValueError()
        return plain[64:]
    except (ValueError, TypeError):
        raise RequestError("invalid_credentials") from None


def trae_cn_storage(home, env):
    home = Path(home)
    override = env.get("TOKENTRACKER_TRAE_CN_HOME")
    if override:
        return Path(override).expanduser() / "User/globalStorage/storage.json"
    app = Path(env.get("APPDATA") or home / "AppData/Roaming") if sys.platform == "win32" else (home / "Library/Application Support" if sys.platform == "darwin" else None)
    if app is None:
        return None
    for name in ("TRAE SOLO CN", "Trae CN"):
        candidate = app / name / "User/globalStorage/storage.json"
        if candidate.exists():
            return candidate
    return None


def trae_cn_token(home, env):
    path = trae_cn_storage(home, env)
    if path is None or not path.exists():
        return None
    try:
        storage = json.loads(path.read_text(encoding="utf-8-sig"))
        value = obj(storage).get("iCubeAuthInfo://icube.cloudide")
        if value is None:
            return None
        if isinstance(value, str):
            value = json.loads(value if value.strip().startswith("{") else decrypt_trae_cn(base64.b64decode(value, validate=True)))
        token = obj(value).get("token")
        if not isinstance(token, str) or not token.strip():
            raise ValueError()
        return token.strip()
    except OSError:
        raise RequestError("credentials_unreadable") from None
    except (ValueError, UnicodeError, TypeError):
        raise RequestError("invalid_credentials") from None


def normalize_trae_cn(sessions):
    result = {}
    for raw in sessions:
        if not isinstance(raw, dict):
            raise RequestError("invalid_response")
        session = raw.get("session_id")
        when = raw.get("usage_time")
        model = raw.get("model_name") or "trae-cn-unknown"
        if not isinstance(session, str) or not session.strip() or type(when) is not int or when <= 0 or not isinstance(model, str) or "|" in model:
            raise RequestError("invalid_response")
        extra = raw.get("extra_info")
        if isinstance(extra, str):
            try:
                extra = json.loads(extra)
            except ValueError:
                extra = None
        extra = obj(extra)
        values = []
        for key in ("input_token", "output_token", "cache_read_token", "cache_write_token"):
            value = extra.get(key, raw.get(key))
            if value is None and key.startswith("cache_"):
                value = 0
            if type(value) is not int or value < 0 or value > 2**53 - 1:
                raise RequestError("invalid_response")
            values.append(value)
        prompt, output, read, write = values
        read, write = min(prompt, read), min(max(0, prompt - read), write)
        if prompt + output > 2**53 - 1:
            raise RequestError("invalid_response")
        usage = totals(prompt - read - write, output, read, write)
        # 会话的最新快照取代原观察；账号级 session id 与设备无关。
        # 显式零用量是修正，不能与缺失会话混为一谈而保留旧的非零值。
        item = event("trae-cn", model.strip(), when, usage if usage["total_tokens"] else totals(1), session.strip(), source_scope="account")
        item.update(usage)
        result[item["event_id"]] = item
    return list(result.values())


def fetch_trae_cn_window(token, start, end, http, *, delay=0.3, max_pages=100, depth=0, sleeper=time.sleep):
    sessions = []
    capacity = False
    for page in range(1, max_pages + 1):
        payload = http(TRAE_CN_USAGE_URL, headers={"Authorization": "Cloud-IDE-JWT " + token},
                       body={"usage_type": [7], "start_time": start, "end_time": end, "page_num": page, "page_size": 20})
        body = obj(payload.get("data")) or obj(payload)
        code = body.get("code", payload.get("code", 0))
        if code not in (0, "0", None):
            raise RequestError("reauth" if code in {401, 403, "401", "403"} else "unavailable")
        rows = body.get("user_usage_group_by_sessions")
        total = body.get("total")
        if not isinstance(rows, list) or (total is not None and (type(total) is not int or total < 0)):
            raise RequestError("invalid_response")
        if total is not None and total > max_pages * 20:
            capacity = True
            break
        sessions.extend(rows)
        if not rows or (total is not None and len(sessions) >= total):
            return sessions
        if page < max_pages and delay:
            sleeper(delay)
    else:
        capacity = True
    if capacity and depth < 8 and end > start:
        middle = start + (end - start) // 2
        options = dict(delay=delay, max_pages=max_pages, depth=depth + 1, sleeper=sleeper)
        return fetch_trae_cn_window(token, start, middle, http, **options) + fetch_trae_cn_window(token, middle + 1, end, http, **options)
    raise RequestError("capacity_exceeded")


def trae_cn_usage(home, env=None, http=None, *, now=None, delay=0.3):
    env = dict(os.environ if env is None else env)
    # 沿用原适配器的显式开关，未启用时不向服务器发送登录信息。
    if str(env.get("TOKENTRACKER_TRAE_CN_USAGE", "")).strip().lower() not in {"1", "true"}:
        return []
    token = trae_cn_token(home, env)
    if not token:
        return []
    end = int(time.time() if now is None else now)
    start = max(1, (end - 30 * 86400) // 1800 * 1800)
    http = http or Http(30)
    for attempt in range(2):
        try:
            return normalize_trae_cn(fetch_trae_cn_window(token, start, end, http, delay=delay))
        except RequestError as error:
            if attempt or (error.status not in {401, 403} and str(error) != "reauth"):
                raise
            token = trae_cn_token(home, env)
            if not token:
                raise


def collect(home, env=None, http=None):
    """每个账号独立成功／失败；失败来源由调用方保留旧快照。"""
    result, errors = {}, {}
    for source, reader in (("cursor", cursor_usage), ("trae-cn", trae_cn_usage)):
        try:
            result[source] = reader(home, env, http)
        except Exception as error:
            errors[source] = str(error) if isinstance(error, RequestError) else "unavailable"
    return result, errors
