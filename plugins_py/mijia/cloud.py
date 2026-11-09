# cloud.py — 米家扫码、签名及云端属性协议（来源见 THIRD_PARTY_NOTICES.md）
from __future__ import annotations

import base64
import hashlib
import http.cookiejar
import json
import secrets
import struct
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Any

REGIONS = ("cn", "sg", "de", "us", "ru")


class CloudError(Exception):
    """异常只携带稳定状态码，避免 traceback 泄露 Cookie 或服务端响应。"""

    def __init__(self, code: str):
        super().__init__(code)
        self.code = code


@dataclass
class Response:
    status: int
    headers: Any
    body: bytes


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def trusted_url(url: str, spec: bool = False) -> str:
    if not isinstance(url, str):
        raise CloudError("invalid_response")
    parsed = urllib.parse.urlsplit(url)
    host = (parsed.hostname or "").lower()
    allowed = host == "miot-spec.org" if spec else host.endswith(".xiaomi.com") or host.endswith(".mi.com")
    if parsed.scheme != "https" or not allowed or parsed.username or parsed.password or parsed.port not in (None, 443):
        raise CloudError("invalid_response")
    return url


class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        trusted_url(newurl, urllib.parse.urlsplit(req.full_url).hostname == "miot-spec.org")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


class Transport:
    def __call__(self, url: str, *, data: bytes | None = None,
                 headers: dict | None = None, redirects: bool = True,
                 timeout: float = 10, limit: int = 16 * 1024 * 1024) -> Response:
        trusted_url(url, urllib.parse.urlsplit(url).hostname == "miot-spec.org")
        opener = urllib.request.build_opener(SafeRedirect() if redirects else NoRedirect())
        request = urllib.request.Request(url, data=data, headers=headers or {})
        try:
            try:
                response = opener.open(request, timeout=timeout)
            except urllib.error.HTTPError as error:
                response = error
            with response:
                body = response.read(limit + 1)
                if len(body) > limit:
                    raise CloudError("invalid_response")
                return Response(response.code, response.headers, body)
        except CloudError:
            raise
        except (TimeoutError, OSError, urllib.error.URLError):
            raise CloudError("network_error") from None


def json_object(body: bytes) -> dict:
    try:
        text = body.decode("utf-8").strip()
        if text.startswith("&&&START&&&"):
            text = text[len("&&&START&&&"):]
        value = json.loads(text)
        if not isinstance(value, dict):
            raise ValueError
        return value
    except (UnicodeError, ValueError):
        raise CloudError("invalid_response") from None


def rc4(data: bytes, key: bytes) -> bytes:
    if not key:
        raise CloudError("invalid_session")
    state = list(range(256))
    j = 0
    for i in range(256):
        j = (j + state[i] + key[i % len(key)]) & 255
        state[i], state[j] = state[j], state[i]
    i = j = 0
    output = bytearray()
    for offset, value in enumerate(bytes(1024) + data):
        i = (i + 1) & 255
        j = (j + state[i]) & 255
        state[i], state[j] = state[j], state[i]
        decoded = value ^ state[(state[i] + state[j]) & 255]
        if offset >= 1024:
            output.append(decoded)
    return bytes(output)


def decode64(value: str) -> bytes:
    try:
        return base64.b64decode(value, validate=True)
    except (ValueError, TypeError):
        raise CloudError("invalid_session") from None


def signed_params(path: str, payload: dict, security: str,
                  nonce: str | None = None) -> tuple[dict[str, str], bytes]:
    nonce = nonce or base64.b64encode(secrets.token_bytes(8) + struct.pack(
        ">I", int(time.time() / 60) & 0xffffffff)).decode("ascii")
    key = hashlib.sha256(decode64(security) + decode64(nonce)).digest()
    signed = base64.b64encode(key).decode("ascii")

    def signature(items):
        text = "&".join(["POST", path] + [f"{k}={v}" for k, v in items] + [signed])
        return base64.b64encode(hashlib.sha1(text.encode()).digest()).decode("ascii")

    plain = json.dumps(payload, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
    fields = {"data": plain, "rc4_hash__": signature([("data", plain)])}
    fields = {k: base64.b64encode(rc4(v.encode(), key)).decode("ascii") for k, v in fields.items()}
    fields["signature"] = signature(fields.items())
    fields.update(ssecurity=security, _nonce=nonce)
    return fields, key


def validate_session(value: Any) -> dict:
    if not isinstance(value, dict) or value.get("region") not in REGIONS:
        raise CloudError("invalid_session")
    for key in ("userId", "cUserId", "ssecurity", "serviceToken"):
        if not isinstance(value.get(key), str) or not value[key] or any(c in value[key] for c in "\r\n;"):
            raise CloudError("invalid_session")
    if not decode64(value["ssecurity"]):
        raise CloudError("invalid_session")
    expiry = value.get("expiresAt")
    if expiry is not None and (not isinstance(expiry, (int, float)) or isinstance(expiry, bool)):
        raise CloudError("invalid_session")
    return {key: value.get(key) for key in ("userId", "cUserId", "ssecurity", "serviceToken", "region", "expiresAt")}


def scope_for(session: dict) -> str:
    return hashlib.sha256(f'{session["region"]}:{session["userId"]}'.encode()).hexdigest()[:24]


class Login:
    def __init__(self, transport=None, clock=time.time):
        self.transport = transport or Transport()
        self.clock = clock

    def start(self, region: str) -> dict:
        if region not in REGIONS:
            raise CloudError("invalid_argument")
        query = urllib.parse.urlencode({"_qrsize": "480", "qs": "%3Fsid%3Dxiaomiio%26_json%3Dtrue",
            "callback": "https://sts.api.io.mi.com/sts", "_hasLogo": "false", "sid": "xiaomiio",
            "serviceParam": "", "_locale": "zh_CN", "_dc": str(int(self.clock() * 1000))})
        response = self.transport("https://account.xiaomi.com/longPolling/loginUrl?" + query)
        if response.status != 200:
            raise CloudError("network_error")
        raw = json_object(response.body)
        try:
            ticket = {"poll": trusted_url(raw["lp"]), "region": region,
                      "deadline": self.clock() + min(600, max(1, float(raw.get("timeout", 300))))}
            image = self.transport(trusted_url(raw["qr"]), limit=512 * 1024)
        except (KeyError, TypeError, ValueError):
            raise CloudError("invalid_response") from None
        if image.status != 200:
            raise CloudError("network_error")
        if image.body.startswith(b"\x89PNG\r\n\x1a\n"):
            mime = "image/png"
        elif image.body.startswith(b"\xff\xd8\xff"):
            mime = "image/jpeg"
        else:
            raise CloudError("invalid_response")
        ticket["image"] = f"data:{mime};base64," + base64.b64encode(image.body).decode("ascii")
        return ticket

    def poll(self, ticket: dict) -> dict | None:
        if self.clock() >= ticket["deadline"]:
            raise CloudError("qr_expired")
        try:
            response = self.transport(ticket["poll"], timeout=min(10, max(1, ticket["deadline"] - self.clock())))
        except CloudError as error:
            if error.code == "network_error":
                return None
            raise
        if response.status != 200 or not response.body.strip():
            return None
        raw = json_object(response.body)
        if "location" not in raw:
            if raw.get("code") in (0, 70016, "70016"):
                return None
            raise CloudError("qr_expired")
        token, expiry = self.exchange(trusted_url(raw["location"]))
        return validate_session({"userId": str(raw.get("userId", "")),
            "cUserId": str(raw.get("cUserId") or raw.get("userId", "")),
            "ssecurity": raw.get("ssecurity"), "serviceToken": token,
            "expiresAt": expiry, "region": ticket["region"]})

    def exchange(self, location: str) -> tuple[str, float | None]:
        jar = http.cookiejar.CookieJar()
        url = location
        for _ in range(5):
            response = self.transport(url, redirects=False)
            request = urllib.request.Request(url)
            # CookieJar 正确处理多个 Set-Cookie、Expires 中的逗号及 Max-Age。
            class CookieResponse:
                def info(self):
                    return response.headers
            jar.extract_cookies(CookieResponse(), request)
            for cookie in jar:
                if cookie.name == "serviceToken" and cookie.value and not cookie.is_expired():
                    return cookie.value, cookie.expires
            next_url = response.headers.get("Location")
            if 300 <= response.status < 400 and next_url:
                url = trusted_url(urllib.parse.urljoin(url, next_url))
            else:
                break
        raise CloudError("invalid_response")


class Client:
    def __init__(self, session: dict, transport=None):
        self.session = validate_session(session)
        self.transport = transport or Transport()

    def request(self, path: str, payload: dict) -> Any:
        if self.session.get("expiresAt") and self.session["expiresAt"] <= time.time():
            raise CloudError("session_expired")
        path = "/" + path.lstrip("/")
        fields, key = signed_params(path, payload, self.session["ssecurity"])
        region = self.session["region"]
        base = "https://api.io.mi.com/app" if region == "cn" else f"https://{region}.api.io.mi.com/app"
        response = self.transport(base + path, data=urllib.parse.urlencode(fields).encode(), headers={
            "User-Agent": "abcdefghijklmnoqrs-ABCDEABCDEABC APP/com.xiaomi.mihome APPV/10.5.201",
            "x-xiaomi-protocal-flag-cli": "PROTOCAL-HTTP2", "MIOT-ENCRYPT-ALGORITHM": "ENCRYPT-RC4",
            "Accept-Encoding": "identity", "Content-Type": "application/x-www-form-urlencoded",
            "Cookie": f'userId={self.session["userId"]}; serviceToken={self.session["serviceToken"]}; '
                      f'yetAnotherServiceToken={self.session["serviceToken"]}; locale=zh_CN; timezone=GMT+08:00; '
                      "is_daylight=0; dst_offset=0; channel=MI_APP_STORE"})
        if response.status in (401, 403):
            raise CloudError("session_expired")
        if response.status == 429:
            raise CloudError("rate_limited")
        if response.status != 200:
            raise CloudError("network_error")
        try:
            raw = json_object(response.body)
        except CloudError:
            try:
                raw = json_object(rc4(decode64(response.body.strip().decode("ascii")), key))
            except (CloudError, UnicodeError):
                raise CloudError("invalid_response") from None
        code = raw.get("code")
        if code != 0:
            message = str(raw.get("message") or raw.get("desc") or "").lower()
            if code in (2, 3) or any(term in message for term in ("auth err", "servicetoken_expired", "token expired")):
                raise CloudError("session_expired")
            raise CloudError("api_error")
        return raw.get("result")

    def devices(self, dids: list[str] | None = None) -> list[dict]:
        found = {}
        start = ""
        for _ in range(100):
            args = {"get_split_device": True, "support_smart_home": True, "limit": 200}
            if dids is not None:
                args["dids"] = dids
            if start:
                args["start_did"] = start
            try:
                raw = self.request("v2/home/device_list_page", args)
            except CloudError as error:
                if error.code != "api_error" or start or dids:
                    raise
                raw = self.request("home/device_list", {"getVirtualModel": True,
                    "getHuamiDevices": 1, "get_split_device": True, "support_smart_home": True})
            if not isinstance(raw, dict) or not isinstance(raw.get("list"), list):
                raise CloudError("invalid_response")
            for item in raw["list"]:
                record = parse_device(item)
                if record:
                    found[record["id"]] = record
            next_id = str(raw.get("next_start_did") or "")
            if not raw.get("has_more"):
                return list(found.values())
            if not next_id or next_id == start:
                raise CloudError("invalid_response")
            start = next_id
        raise CloudError("invalid_response")

    def discover(self) -> tuple[list[dict], list[dict]]:
        try:
            raw = self.request("v2/homeroom/gethome_merged", {"fg": True, "fetch_share": True,
                "fetch_share_dev": True, "limit": 300, "app_ver": 7, "plat_form": 0})
        except CloudError as error:
            if error.code != "api_error":
                raise
            raw = self.request("v2/homeroom/gethome", {"fg": True, "fetch_share": True,
                "fetch_share_dev": True, "limit": 300, "app_ver": 7})
        homes = parse_homes(raw)
        devices = {d["id"]: d for d in self.devices()}
        missing = list(dict.fromkeys(did for home in homes for did in home["dids"] if did not in devices))
        for start in range(0, len(missing), 150):
            devices.update((d["id"], d) for d in self.devices(missing[start:start + 150]))
        for device in devices.values():
            device["homes"] = [h["id"] for h in homes if device["id"] in h["dids"] or device["cloudDid"] in h["dids"]]
            device["rooms"] = [r["id"] for h in homes for r in h["rooms"]
                               if device["id"] in r["dids"] or device["cloudDid"] in r["dids"]]
        return list(devices.values()), homes

    def read(self, keys: list[dict]) -> list[dict]:
        results = []
        for start in range(0, len(keys), 100):
            raw = self.request("miotspec/prop/get", {"params": keys[start:start + 100]})
            if not isinstance(raw, list):
                raise CloudError("invalid_response")
            results.extend(raw)
        return results

    def write(self, key: dict, value: Any):
        result = self.request("miotspec/prop/set", {"params": [dict(key, value=value)]})
        if not isinstance(result, list) or len(result) != 1 or result[0].get("code") != 0:
            raise CloudError("device_error")

    def action(self, did: str, siid: int, aiid: int):
        raw = self.request("miotspec/action", {"params": {"did": did, "siid": siid, "aiid": aiid, "in": []}})
        if not isinstance(raw, dict) or raw.get("code") != 0:
            raise CloudError("device_error")


def parse_device(raw: Any) -> dict | None:
    if not isinstance(raw, dict) or not raw.get("did") or not isinstance(raw.get("model"), str):
        return None
    did = str(raw["did"])
    extra = raw.get("extra") if isinstance(raw.get("extra"), dict) else {}
    split_info = extra.get("split") if isinstance(extra.get("split"), dict) else {}
    parent = split_info.get("parentId")
    split = split_info.get("moduleId")
    if not parent or type(split) is not int or split < 1:
        parent, split = did, None
    return {"id": did, "cloudDid": str(parent),
            "splitSiid": split, "name": str(raw.get("name") or raw["model"]), "model": raw["model"],
            "online": raw.get("isOnline") is True, "specType": raw.get("spec_type") or extra.get("spec_type")}


def parse_homes(raw: Any) -> list[dict]:
    if not isinstance(raw, dict):
        raise CloudError("invalid_response")
    homes = {}
    for group in ("homelist", "home_list", "share_home_list", "share_homelist"):
        entries = raw.get(group, [])
        if not isinstance(entries, list):
            continue
        for item in entries:
            if not isinstance(item, dict) or item.get("id") is None:
                continue
            hid = str(item["id"])
            if hid in homes:
                continue
            rooms = []
            dids = [str(d) for d in item.get("dids", []) if isinstance(d, (str, int))]
            for room in item.get("roomlist", []):
                if not isinstance(room, dict) or room.get("id") is None:
                    continue
                rdids = [str(d) for d in room.get("dids", []) if isinstance(d, (str, int))]
                rooms.append({"id": hid + ":" + str(room["id"]), "name": str(room.get("name", "")), "dids": rdids})
                dids.extend(rdids)
            homes[hid] = {"id": hid, "name": str(item.get("name", "")), "shared": group.startswith("share"),
                          "rooms": rooms, "dids": list(dict.fromkeys(dids))}
    return list(homes.values())
