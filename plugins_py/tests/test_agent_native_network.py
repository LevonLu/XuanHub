# test_agent_native_network.py — 原生 HTTP、HTTPS、SOCKS5 代理与迁移的本地协议验收
from contextlib import ExitStack
from datetime import datetime, timedelta, timezone
import ipaddress
import json
import os
from pathlib import Path
import select
import socket
import ssl
import struct
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "agent_usage"))
from native import network


def headers(sock):
    raw = bytearray()
    while not raw.endswith(b"\r\n\r\n"):
        raw.extend(network.recv_exact(sock, 1))
        if len(raw) > 65536:
            raise ValueError("oversized fixture headers")
    return bytes(raw)


class ProxyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.addCleanup(network.configure_proxy, "system")

    def test_auto_migrates_once_and_off_does_not_use_environment(self):
        source = self.root / ".tokentracker/tracker/config.json"
        source.parent.mkdir(parents=True)
        source.write_text(json.dumps({"proxy": {"mode": "off"}}))
        with patch.object(Path, "home", return_value=self.root), patch.dict(os.environ, {"HTTPS_PROXY": "http://proxy.invalid:9999"}):
            network.configure_proxy(cache_dir=self.root / "cache")
            self.assertIsNone(network.effective_proxy("api.example.com"))
            source.write_text(json.dumps({"proxy": {"mode": "manual", "host": "changed", "protocol": "http", "port": 8888}}))
            network.configure_proxy(cache_dir=self.root / "cache")
            self.assertIsNone(network.effective_proxy("api.example.com"))
        self.assertEqual(json.loads((self.root / "cache/proxy.json").read_text())["mode"], "off")

    def test_auto_migrates_manual_socks_to_independent_cache(self):
        source = self.root / ".tokentracker/tracker/config.json"
        source.parent.mkdir(parents=True)
        source.write_text(json.dumps({"proxy": {"mode": "manual", "protocol": "socks5", "host": "::1", "port": 1080}}))
        with patch.object(Path, "home", return_value=self.root):
            network.configure_proxy(cache_dir=self.root / "cache")
            self.assertEqual(network.effective_proxy("example.com"), "socks5://[::1]:1080")
        self.assertEqual(json.loads((self.root / "cache/proxy.json").read_text())["url"], "socks5://[::1]:1080")

    def test_invalid_manual_never_falls_back_to_direct(self):
        for url in ("", "file:///tmp/secret", "http://host:99999", "socks5://host/path", "https://host/?token=secret"):
            with self.subTest(url=url):
                with self.assertRaisesRegex(network.RequestError, "proxy_failed"):
                    network.configure_proxy("manual", url)
                with self.assertRaisesRegex(network.RequestError, "proxy_failed"):
                    network.effective_proxy("example.com")

    def test_system_all_proxy_and_no_proxy(self):
        network.configure_proxy("system")
        with patch.object(urllib.request, "getproxies", return_value={"all": "socks5://127.0.0.1:1080"}), patch.object(urllib.request, "proxy_bypass", side_effect=lambda host: host == "localhost"):
            self.assertEqual(network.effective_proxy("example.com"), "socks5://127.0.0.1:1080")
            self.assertIsNone(network.effective_proxy("localhost"))

    def test_raw_body_and_json_are_exclusive(self):
        with self.assertRaisesRegex(network.RequestError, "invalid_request"):
            network.Http()("https://example.invalid", raw_body=b"", body={})

    def contexts(self):
        from cryptography import x509
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import rsa
        from cryptography.x509.oid import NameOID
        key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")])
        cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
                .serial_number(x509.random_serial_number()).not_valid_before(datetime.now(timezone.utc) - timedelta(minutes=1))
                .not_valid_after(datetime.now(timezone.utc) + timedelta(days=1))
                .add_extension(x509.SubjectAlternativeName([x509.DNSName("localhost"), x509.IPAddress(ipaddress.ip_address("127.0.0.1"))]), critical=False)
                .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True).sign(key, hashes.SHA256()))
        cert_path, key_path = self.root / "cert.pem", self.root / "key.pem"
        cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
        key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        server = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        server.load_cert_chain(cert_path, key_path)
        client = ssl.create_default_context(cafile=cert_path)
        return server, client

    def roundtrip(self, protocol):
        server_context, client_context = self.contexts()
        origin = socket.socket()
        origin.bind(("127.0.0.1", 0))
        origin.listen(1)
        origin.settimeout(4)
        proxy = socket.socket()
        proxy.bind(("127.0.0.1", 0))
        proxy.listen(1)
        proxy.settimeout(4)
        captured, failures = {}, []
        def serve_origin():
            try:
                raw, _ = origin.accept()
                raw.settimeout(4)
                with server_context.wrap_socket(raw, server_side=True) as stream:
                    captured["origin"] = headers(stream)
                    body = json.dumps({"ok": True, "padding": "x" * 131072}).encode()
                    stream.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: ' + str(len(body)).encode() + b'\r\nConnection: close\r\n\r\n')
                    time.sleep(.03)
                    stream.sendall(body)
            except Exception as error:
                failures.append(type(error).__name__)
        def serve_proxy():
            try:
                raw, _ = proxy.accept()
                raw.settimeout(4)
                with ExitStack() as stack:
                    downstream = stack.enter_context(server_context.wrap_socket(raw, server_side=True) if protocol == "https" else raw)
                    if protocol == "socks5":
                        version, length = network.recv_exact(downstream, 2)
                        captured["greeting"] = (version, network.recv_exact(downstream, length))
                        downstream.sendall(b"\x05\x02")
                        version, length = network.recv_exact(downstream, 2)
                        captured["user"] = network.recv_exact(downstream, length)
                        length = network.recv_exact(downstream, 1)[0]
                        captured["password"] = network.recv_exact(downstream, length)
                        downstream.sendall(b"\x01\x00")
                        self.assertEqual(network.recv_exact(downstream, 4), b"\x05\x01\x00\x03")
                        length = network.recv_exact(downstream, 1)[0]
                        captured["target"] = network.recv_exact(downstream, length)
                        port = struct.unpack(">H", network.recv_exact(downstream, 2))[0]
                        self.assertEqual(port, origin.getsockname()[1])
                        downstream.sendall(b"\x05\x00\x00\x01\x7f\x00\x00\x01\x00\x00")
                    else:
                        captured["connect"] = headers(downstream)
                        downstream.sendall(b"HTTP/1.1 200 Connection established\r\n\r\n")
                    upstream = stack.enter_context(socket.create_connection(origin.getsockname(), timeout=4))
                    while True:
                        ready, _, _ = select.select([downstream, upstream], [], [], 4)
                        if not ready:
                            return
                        for source in ready:
                            chunk = source.recv(65536)
                            if not chunk:
                                return
                            (upstream if source is downstream else downstream).sendall(chunk)
            except Exception as error:
                failures.append(type(error).__name__)
        threads = [threading.Thread(target=serve_origin, daemon=True), threading.Thread(target=serve_proxy, daemon=True)]
        for thread in threads:
            thread.start()
        network.configure_proxy("manual", f"{protocol}://fixture:password@localhost:{proxy.getsockname()[1]}")
        client = network.Http(timeout=4)
        client.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), network.ProxyHTTPSHandler(context=client_context), network.NoRedirect())
        try:
            response = client(f"https://localhost:{origin.getsockname()[1]}/quota", headers={"Authorization": "Bearer fixture-token"})
            self.assertTrue(response["ok"])
            self.assertEqual(len(response["padding"]), 131072)
        finally:
            for thread in threads:
                thread.join(5)
            proxy.close()
            origin.close()
        self.assertFalse(failures)
        self.assertNotIn(b"Proxy-Authorization", captured["origin"])
        self.assertIn(b"Authorization: Bearer fixture-token", captured["origin"])
        if protocol == "socks5":
            self.assertEqual(captured["user"], b"fixture")
            self.assertEqual(captured["password"], b"password")
            self.assertEqual(captured["target"], b"localhost")
        else:
            self.assertIn(b"Proxy-Authorization: Basic ", captured["connect"])
            self.assertNotIn(b"Bearer fixture-token", captured["connect"])

    def test_http_connect_proxy(self):
        self.roundtrip("http")

    def test_https_tls_inside_tls_proxy(self):
        self.roundtrip("https")

    def test_socks5_auth_and_remote_dns(self):
        self.roundtrip("socks5")


if __name__ == "__main__":
    unittest.main()
