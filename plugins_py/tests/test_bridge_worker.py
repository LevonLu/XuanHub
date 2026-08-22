# test_bridge_worker.py — 隔离 Python worker 的 socket 协议与 stop 生命周期回归
from __future__ import annotations

import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKER = ROOT / "bridge_worker.py"


def send_frame(sock: socket.socket, message: dict) -> None:
    payload = json.dumps(message, separators=(",", ":")).encode("utf-8")
    sock.sendall(struct.pack("!I", len(payload)) + payload)


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise EOFError("worker closed socket")
        data.extend(chunk)
    return bytes(data)


def recv_frame(sock: socket.socket) -> dict:
    size = struct.unpack("!I", recv_exact(sock, 4))[0]
    return json.loads(recv_exact(sock, size).decode("utf-8"))


class BridgeWorkerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.plugin_dir = Path(self.temp_dir.name) / "demo"
        self.plugin_dir.mkdir()
        self.stop_file = Path(self.temp_dir.name) / "stopped.txt"
        locale_callback = "" if self._testMethodName == "test_missing_set_locale_is_compatible" else (
            "def set_locale(locale):\n"
            "    state['locale'] = locale\n"
        )
        config_callback = "" if self._testMethodName == "test_missing_set_locale_is_compatible" else (
            "def set_config(config):\n"
            "    state['prefix'] = config.get('prefix', '')\n"
        )
        (self.plugin_dir / "main.py").write_text(
            "import os\n"
            "state = {'count': 0, 'prefix': '', 'locale': 'unset'}\n"
            "def init(config):\n"
            "    state['prefix'] = config.get('prefix', '')\n"
            "def update():\n"
            "    return {'count': state['count'], 'prefix': state['prefix'], 'locale': state['locale']}\n"
            "def invoke(name, args):\n"
            "    if name == 'bump': state['count'] += int(args[0])\n"
            "    return state['count']\n"
            + locale_callback + config_callback +
            "def stop():\n"
            "    open(os.environ['XUANHUB_TEST_STOP_FILE'], 'w', encoding='utf-8').write('stopped')\n",
            encoding="utf-8",
        )
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.bind(("127.0.0.1", 0))
        self.server.listen(1)
        self.server.settimeout(10)
        self.token = "a" * 64
        environment = dict(os.environ)
        environment["XUANHUB_TEST_STOP_FILE"] = str(self.stop_file)
        self.process = subprocess.Popen(
            [sys.executable, "-E", "-u", str(WORKER), "--port", str(self.server.getsockname()[1]),
             "--token", self.token, "--plugin-dir", str(self.plugin_dir), "--plugin-id", "py_demo"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=environment,
        )
        self.socket, _ = self.server.accept()
        self.socket.settimeout(5)
        self.assertEqual(recv_frame(self.socket), {"type": "hello", "token": self.token})
        self.request_id = 0

    def tearDown(self) -> None:
        if hasattr(self, "socket"):
            self.socket.close()
        if hasattr(self, "server"):
            self.server.close()
        if hasattr(self, "process") and self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=5)
        if hasattr(self, "process"):
            if self.process.stdout:
                self.process.stdout.close()
            if self.process.stderr:
                self.process.stderr.close()
        if hasattr(self, "temp_dir"):
            self.temp_dir.cleanup()

    def request(self, op: str, data: dict) -> dict:
        self.request_id += 1
        request_id = str(self.request_id)
        send_frame(self.socket, {"type": "request", "id": request_id, "op": op, "data": data})
        response = recv_frame(self.socket)
        self.assertEqual(response["type"], "response")
        self.assertEqual(response["id"], request_id)
        return response

    def test_init_update_invoke_and_stop(self) -> None:
        self.assertEqual(self.request("init", {"config": {"prefix": "worker"}, "locale": "en-US"}),
                         {"type": "response", "id": "1", "ok": True,
                          "result": {"ready": True, "capabilities": ["set_config"]}})
        self.assertEqual(self.request("update", {}),
                         {"type": "response", "id": "2", "ok": True,
                          "result": {"count": 0, "prefix": "worker", "locale": "en-US"}})
        self.assertTrue(self.request("set_locale", {"locale": "zh-CN"})["ok"])
        self.assertEqual(self.request("update", {})["result"]["locale"], "zh-CN")
        self.assertTrue(self.request("set_config", {"config": {"prefix": "hot"}})["ok"])
        self.assertEqual(self.request("update", {})["result"]["prefix"], "hot")
        self.assertEqual(self.request("invoke", {"name": "bump", "args": [3]})["result"], 3)
        self.assertEqual(self.request("update", {})["result"]["count"], 3)
        self.assertTrue(self.request("stop", {})["ok"])
        self.process.wait(timeout=5)
        self.assertEqual(self.process.returncode, 0, self.process.stderr.read())
        self.assertEqual(self.stop_file.read_text(encoding="utf-8"), "stopped")

    def test_missing_set_locale_is_compatible(self) -> None:
        initialized = self.request("init", {"config": {}, "locale": "en-US"})
        self.assertTrue(initialized["ok"])
        self.assertEqual(initialized["result"]["capabilities"], [])
        self.assertTrue(self.request("set_locale", {"locale": "zh-CN"})["ok"])
        self.assertEqual(self.request("update", {})["result"]["locale"], "unset")
        self.assertTrue(self.request("stop", {})["ok"])
        self.process.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
