# network.py — 有界 HTTP 请求、稳定错误码与凭据原子更新
from __future__ import annotations

import base64
import http.client
import io
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import time
import urllib.error
import urllib.parse
import urllib.request

from .common import obj, read_json

_proxy_state = {"mode": "system", "url": ""}


def proxy_url(value):
    """只接受明确代理协议；凭据永不成为错误文本。"""
    try:
        parsed = urllib.parse.urlsplit(str(value))
        if parsed.scheme not in {"http", "https", "socks5", "socks5h"} or not parsed.hostname or parsed.path not in {"", "/"} or parsed.query or parsed.fragment:
            raise ValueError()
        if any(c.isspace() or c in "\\\r\n" for c in parsed.hostname):
            raise ValueError()
        port = parsed.port or (443 if parsed.scheme == "https" else 1080 if parsed.scheme.startswith("socks") else 80)
        if not 1 <= port <= 65535:
            raise ValueError()
        return parsed, port
    except (TypeError, ValueError):
        raise RequestError("proxy_failed") from None


def configure_proxy(mode="auto", url="", cache_dir=None):
    """兼容一次旧设置后使用插件自己的缓存；显式代理始终失败关闭。"""
    global _proxy_state
    _proxy_state = {"mode": "blocked", "url": ""}
    if mode not in {"auto", "system", "off", "manual"}:
        raise RequestError("proxy_failed")
    if mode == "auto":
        path = Path(cache_dir) / "proxy.json" if cache_dir else None
        if path and path.exists():
            saved = obj(read_json(path))
            mode, url = saved.get("mode"), saved.get("url", "")
        else:
            legacy = obj(obj(read_json(Path.home() / ".tokentracker/tracker/config.json")).get("proxy"))
            mode = legacy.get("mode") or "system"
            url = ""
            if mode == "manual":
                protocol = legacy.get("protocol")
                host = str(legacy.get("host") or "").strip()
                if protocol == "socks":
                    protocol = "socks5"
                if not host or any(c in host for c in "@/?#\\") or any(c.isspace() for c in host):
                    raise RequestError("proxy_failed")
                if ":" in host and not host.startswith("["):
                    host = "[" + host + "]"
                url = f"{protocol}://{host}:{legacy.get('port')}"
                proxy_url(url)
            if mode not in {"system", "off", "manual"}:
                mode = "system"
            if path:
                path.parent.mkdir(parents=True, exist_ok=True)
                temp = path.with_name(f"proxy-{os.getpid()}-{time.time_ns()}.tmp")
                try:
                    temp.write_text(json.dumps({"version": 1, "mode": mode, "url": url}), encoding="utf-8")
                    os.chmod(temp, 0o600)
                    os.replace(temp, path)
                finally:
                    temp.unlink(missing_ok=True)
    if mode == "manual":
        proxy_url(url)
    elif mode not in {"off", "system"}:
        raise RequestError("proxy_failed")
    _proxy_state = {"mode": mode, "url": url if mode == "manual" else ""}


def effective_proxy(host):
    state = dict(_proxy_state)
    if state["mode"] == "blocked":
        raise RequestError("proxy_failed")
    if state["mode"] == "off":
        return None
    if state["mode"] == "manual":
        return state["url"]
    proxies = urllib.request.getproxies()
    if urllib.request.proxy_bypass(host):
        return None
    value = proxies.get("https") or proxies.get("http") or proxies.get("all")
    if value and "://" not in value:
        value = "http://" + value
    if value:
        proxy_url(value)
    return value


def recv_exact(sock, size):
    raw = bytearray()
    while len(raw) < size:
        chunk = sock.recv(size - len(raw))
        if not chunk:
            raise RequestError("proxy_failed")
        raw.extend(chunk)
    return bytes(raw)


def socks_connect(sock, target, port, proxy):
    username = urllib.parse.unquote(proxy.username or "").encode()
    password = urllib.parse.unquote(proxy.password or "").encode()
    if len(username) > 255 or len(password) > 255:
        raise RequestError("proxy_failed")
    sock.sendall(b"\x05\x02\x00\x02" if username else b"\x05\x01\x00")
    version, method = recv_exact(sock, 2)
    if version != 5 or method not in (0, 2):
        raise RequestError("proxy_failed")
    if method == 2:
        if not username:
            raise RequestError("proxy_failed")
        sock.sendall(b"\x01" + bytes([len(username)]) + username + bytes([len(password)]) + password)
        if recv_exact(sock, 2) != b"\x01\x00":
            raise RequestError("proxy_failed")
    host = target.encode("idna")
    if not host or len(host) > 255:
        raise RequestError("proxy_failed")
    # 域名交给代理解析，既兼容 socks5h，也避免本机 DNS 泄漏或无法解析海外域名。
    sock.sendall(b"\x05\x01\x00\x03" + bytes([len(host)]) + host + struct.pack(">H", port))
    version, status, _, address_type = recv_exact(sock, 4)
    if version != 5 or status != 0:
        raise RequestError("proxy_failed")
    if address_type == 1:
        length = 4
    elif address_type == 4:
        length = 16
    elif address_type == 3:
        length = recv_exact(sock, 1)[0]
    else:
        raise RequestError("proxy_failed")
    recv_exact(sock, length + 2)


class TlsStream:
    """SSLObject 通过已有连接握手，支持 HTTPS 代理里的第二层 TLS。"""
    def __init__(self, transport, context, hostname):
        self.transport = transport
        self.incoming, self.outgoing = ssl.MemoryBIO(), ssl.MemoryBIO()
        self.ssl = context.wrap_bio(self.incoming, self.outgoing, server_hostname=hostname)
        self._closed = False
        self._io_refs = 0
        self._run(self.ssl.do_handshake)

    def _flush(self):
        while self.outgoing.pending:
            self.transport.sendall(self.outgoing.read())

    def _run(self, operation, *args):
        while True:
            try:
                value = operation(*args)
                self._flush()
                return value
            except ssl.SSLWantWriteError:
                self._flush()
            except ssl.SSLWantReadError:
                self._flush()
                raw = self.transport.recv(65536)
                if not raw:
                    self.incoming.write_eof()
                else:
                    self.incoming.write(raw)

    def sendall(self, data):
        view = memoryview(data)
        while view:
            count = self._run(self.ssl.write, view)
            view = view[count:]

    def send(self, data):
        return self._run(self.ssl.write, data)

    def recv(self, size):
        try:
            return self._run(self.ssl.read, size)
        except (ssl.SSLZeroReturnError, ssl.SSLEOFError):
            return b""

    def recv_into(self, buffer, size=0):
        raw = self.recv(size or len(buffer))
        buffer[:len(raw)] = raw
        return len(raw)

    def gettimeout(self):
        return self.transport.gettimeout()

    def settimeout(self, value):
        return self.transport.settimeout(value)

    def makefile(self, mode="rb", buffering=None):
        self._io_refs += 1
        return io.BufferedReader(socket.SocketIO(self, mode), buffering or io.DEFAULT_BUFFER_SIZE)

    def _decref_socketios(self):
        self._io_refs -= 1
        if self._closed and not self._io_refs:
            self.transport.close()

    def close(self):
        self._closed = True
        if not self._io_refs:
            self.transport.close()


class ProxyHTTPSConnection(http.client.HTTPSConnection):
    def connect(self):
        url = effective_proxy(self.host)
        if not url:
            return super().connect()
        proxy, port = proxy_url(url)
        transport = socket.create_connection((proxy.hostname, port), self.timeout, self.source_address)
        try:
            if proxy.scheme == "https":
                transport = self._context.wrap_socket(transport, server_hostname=proxy.hostname)
            if proxy.scheme.startswith("socks"):
                socks_connect(transport, self.host, self.port, proxy)
            else:
                target = f"[{self.host}]:{self.port}" if ":" in self.host else f"{self.host}:{self.port}"
                headers = [f"CONNECT {target} HTTP/1.1", f"Host: {target}"]
                if proxy.username is not None:
                    credentials = urllib.parse.unquote(proxy.username) + ":" + urllib.parse.unquote(proxy.password or "")
                    headers.append("Proxy-Authorization: Basic " + base64.b64encode(credentials.encode()).decode())
                transport.sendall(("\r\n".join(headers) + "\r\n\r\n").encode("ascii"))
                data = bytearray()
                while not data.endswith(b"\r\n\r\n"):
                    data.extend(recv_exact(transport, 1))
                    if len(data) > 65536:
                        raise RequestError("proxy_failed")
                status = bytes(data).split(b"\r\n", 1)[0].split()
                if len(status) < 2 or status[1] != b"200":
                    raise RequestError("proxy_failed")
            self.sock = TlsStream(transport, self._context, self.host) if proxy.scheme == "https" else self._context.wrap_socket(transport, server_hostname=self.host)
        except Exception:
            transport.close()
            raise


class ProxyHTTPSHandler(urllib.request.HTTPSHandler):
    def https_open(self, request):
        return self.do_open(ProxyHTTPSConnection, request, context=self._context)


def build_opener(redirect_handler=None):
    return urllib.request.build_opener(urllib.request.ProxyHandler({}), ProxyHTTPSHandler(), redirect_handler or NoRedirect())


class RequestError(RuntimeError):
    def __init__(self, code, status=0, retry_after=0):
        super().__init__(code)
        self.status, self.retry_after = status, retry_after


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # 登录令牌只允许发送到适配器明确选定的端点。
        raise RequestError("redirect_rejected", code)


class Http:
    def __init__(self, timeout=12):
        self.timeout = timeout
        self.opener = build_opener()

    def __call__(self, url, *, headers=None, body=None, form=None, raw=False, raw_body=None):
        if urllib.parse.urlsplit(url).scheme != "https":
            raise RequestError("invalid_endpoint")
        request_headers = {"Accept": "application/json", "User-Agent": "XuanHub-AgentUsage/0.2", **(headers or {})}
        if sum(value is not None for value in (body, form, raw_body)) > 1:
            raise RequestError("invalid_request")
        data = raw_body
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            request_headers.setdefault("Content-Type", "application/json")
        if form is not None:
            data = urllib.parse.urlencode(form).encode("utf-8")
            request_headers["Content-Type"] = "application/x-www-form-urlencoded"
        request = urllib.request.Request(url, data=data, headers=request_headers)
        try:
            with self.opener.open(request, timeout=self.timeout) as response:
                payload = response.read(8 * 1024 * 1024 + 1)
                if len(payload) > 8 * 1024 * 1024:
                    raise RequestError("response_too_large")
                return payload if raw else json.loads(payload)
        except urllib.error.HTTPError as error:
            # 不传播原始错误正文，避免账号或请求信息进入日志／QML。
            try:
                retry = min(86400, max(0, int(error.headers.get("Retry-After", 0))))
            except ValueError:
                retry = 0
            error.close()
            raise RequestError("reauth" if error.code in {401, 403} else "unavailable", error.code, retry) from None
        except (urllib.error.URLError, socket.timeout, TimeoutError, OSError, http.client.HTTPException):
            raise RequestError("unavailable") from None
        except (ValueError, UnicodeError):
            raise RequestError("invalid_response") from None


def bearer(token):
    return {"Authorization": "Bearer " + token}


def jwt(token):
    try:
        value = token.split(".")[1]
        return obj(json.loads(base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))))
    except (AttributeError, IndexError, ValueError, UnicodeError):
        return {}


def persist_credentials(path, previous, updated):
    # 刷新期间 Agent 可能自行换号；不覆盖已变化的登录文件。
    path = Path(path)
    if read_json(path) != previous:
        raise RequestError("credentials_changed")
    temporary = path.with_name(path.name + f".xuanhub-{os.getpid()}-{time.time_ns()}.tmp")
    try:
        with temporary.open("x", encoding="utf-8") as stream:
            json.dump(updated, stream, ensure_ascii=False, indent=2)
        os.chmod(temporary, 0o600)
        if read_json(path) != previous:
            raise RequestError("credentials_changed")
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)
