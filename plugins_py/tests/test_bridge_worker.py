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
        host_import = ""
        host_init = ""
        migration_code = ""
        if self._testMethodName == "test_config_migration_runs_before_init":
            migration_code = (
                "CONFIG_VERSION = 3\n"
                "def migrate_config(old_version, config):\n"
                "    config['prefix'] = config.get('legacyPrefix', '')\n"
                "    config['migratedFrom'] = old_version\n"
                "    return config\n"
                "def validate_config(config):\n"
                "    return {'ok': bool(config.get('prefix')), 'errors': ['prefix required']}\n"
            )
        elif self._testMethodName == "test_invalid_config_migration_is_rejected":
            migration_code = (
                "CONFIG_VERSION = 2\n"
                "def migrate_config(old_version, config):\n"
                "    return []\n"
            )
        elif self._testMethodName == "test_validate_config_does_not_apply_it":
            migration_code = (
                "def validate_config(config):\n"
                "    ok = config.get('prefix') == 'allowed'\n"
                "    return {'ok': ok, 'errors': [] if ok else ['prefix rejected']}\n"
            )
        if self._testMethodName == "test_host_services_are_available_during_init":
            host_import = "from xuanhub_host import log, notify\n"
            host_init = (
                "    state['log_ok'] = log('info', 'worker ready', {'kind': 'test'})['success']\n"
                "    state['notify_ok'] = notify('Demo', 'Ready')['success']\n"
            )
        if self._testMethodName == "test_event_bus_publish_subscribe_and_request":
            host_import = "from xuanhub_host import events\n"
            host_init = (
                "    state['event_value'] = 0\n"
                "    def on_event(envelope):\n"
                "        state['event_value'] = envelope.get('data', {}).get('value', 0)\n"
                "    state['sub_ok'] = events.subscribe('plugin.py_demo.input', on_event, True)['success']\n"
                "    state['pub_ok'] = events.publish('plugin.py_demo.ready', {'value': 1})['success']\n"
            )
        (self.plugin_dir / "main.py").write_text(
            "import os\n"
            + host_import + migration_code +
            "state = {'count': 0, 'prefix': '', 'locale': 'unset'}\n"
            "def init(config):\n"
            "    state['prefix'] = config.get('prefix', '')\n"
            + host_init +
            "def update():\n"
            "    return dict(state)\n"
            "def invoke(name, args):\n"
            "    if name == 'bump': state['count'] += int(args[0])\n"
            "    if name == 'request': state['request'] = events.request('plugin.py_demo.query', {'value': 2})\n"
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
        self.host_requests: list[dict] = []
        self.event_requests: list[dict] = []

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
        while True:
            response = recv_frame(self.socket)
            if response.get("type") == "host_service":
                self.host_requests.append(response)
                send_frame(self.socket, {
                    "type": "host_service_response",
                    "id": response["id"],
                    "ok": True,
                    "result": None,
                })
                continue
            if response.get("type") == "event_bus":
                self.event_requests.append(response)
                result = {"accepted": True}
                if response.get("operation") == "request":
                    result = {"requestId": "bus-1", "success": True,
                              "data": {"answer": 42}, "errorCode": "",
                              "errorMessage": ""}
                send_frame(self.socket, {
                    "type": "event_bus_response",
                    "id": response["id"],
                    "ok": True,
                    "result": result,
                })
                continue
            else:
                break
        self.assertEqual(response["type"], "response")
        self.assertEqual(response["id"], request_id)
        return response

    def test_init_update_invoke_and_stop(self) -> None:
        self.assertEqual(self.request("init", {"config": {"prefix": "worker"}, "locale": "en-US"}),
                         {"type": "response", "id": "1", "ok": True,
                          "result": {"ready": True,
                                     "capabilities": ["set_config", "host_services.v1",
                                                      "event_bus.v2", "config_migration.v1"],
                                     "migration": {"required": False,
                                                   "config": {"prefix": "worker"}}}})
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
        self.assertEqual(initialized["result"]["capabilities"],
                         ["host_services.v1", "event_bus.v2", "config_migration.v1"])
        self.assertTrue(self.request("set_locale", {"locale": "zh-CN"})["ok"])
        self.assertEqual(self.request("update", {})["result"]["locale"], "unset")
        self.assertTrue(self.request("stop", {})["ok"])
        self.process.wait(timeout=5)

    def test_host_services_are_available_during_init(self) -> None:
        initialized = self.request("init", {"config": {}, "locale": "en-US"})
        self.assertTrue(initialized["ok"])
        self.assertEqual(
            [(request["service"], request["method"]) for request in self.host_requests],
            [("logger", "log"), ("notifications", "show")],
        )
        self.assertTrue(all("pluginId" not in request for request in self.host_requests))
        self.assertTrue(self.request("stop", {})["ok"])
        self.process.wait(timeout=5)

    def test_event_bus_publish_subscribe_and_request(self) -> None:
        initialized = self.request("init", {"config": {}, "locale": "en-US"})
        self.assertTrue(initialized["ok"])
        self.assertEqual([item["operation"] for item in self.event_requests],
                         ["subscribe", "publish"])
        self.assertTrue(all("pluginId" not in item for item in self.event_requests))

        send_frame(self.socket, {
            "type": "event_bus_event", "id": "delivery-1",
            "topic": "plugin.py_demo.input",
            "envelope": {"schemaVersion": 1, "source": "plugin.test",
                         "kind": "event", "data": {"value": 9}},
        })
        self.assertEqual(recv_frame(self.socket),
                         {"type": "event_bus_event_ack", "id": "delivery-1"})
        self.assertEqual(self.request("update", {})["result"]["event_value"], 9)
        self.assertTrue(self.request("invoke", {"name": "request", "args": []})["ok"])
        state = self.request("update", {})["result"]
        self.assertEqual(state["request"]["data"]["answer"], 42)

    def test_config_migration_runs_before_init(self) -> None:
        initialized = self.request("init", {
            "config": {"legacyPrefix": "migrated"},
            "configVersion": 1,
            "targetConfigVersion": 3,
            "locale": "en-US",
        })
        self.assertTrue(initialized["ok"], initialized.get("error"))
        self.assertEqual(initialized["result"]["migration"], {
            "required": True,
            "config": {"legacyPrefix": "migrated", "prefix": "migrated",
                       "migratedFrom": 1},
        })
        self.assertEqual(self.request("update", {})["result"]["prefix"], "migrated")

    def test_invalid_config_migration_is_rejected(self) -> None:
        initialized = self.request("init", {
            "config": {}, "configVersion": 1,
            "targetConfigVersion": 2, "locale": "en-US",
        })
        self.assertFalse(initialized["ok"])
        self.assertIn("migrate_config() must return a dict", initialized["error"])

    def test_validate_config_does_not_apply_it(self) -> None:
        initialized = self.request("init", {
            "config": {"prefix": "allowed"}, "locale": "en-US",
        })
        self.assertTrue(initialized["ok"], initialized.get("error"))
        self.assertIn("validate_config", initialized["result"]["capabilities"])
        accepted = self.request("validate_config", {"config": {"prefix": "allowed"}})
        self.assertEqual(accepted["result"], {
            "supported": True, "valid": True, "error": "",
        })
        rejected = self.request("validate_config", {"config": {"prefix": "blocked"}})
        self.assertEqual(rejected["result"], {
            "supported": True, "valid": False, "error": "prefix rejected",
        })
        self.assertEqual(self.request("update", {})["result"]["prefix"], "allowed")


if __name__ == "__main__":
    unittest.main()
