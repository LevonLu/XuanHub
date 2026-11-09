# test_mijia.py — 米家协议、设备能力、会话隔离及 QML 交互回归
from __future__ import annotations

import base64
import copy
import email.message
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock

ROOT = Path(__file__).resolve().parents[2]
PLUGIN = ROOT / "plugins_py/mijia"
FIXTURES = Path(__file__).parent / "fixtures/mijia"
sys.path.insert(0, str(PLUGIN))
from cloud import Client, CloudError, Login, Response, parse_device, parse_homes, rc4, scope_for, signed_params
from specs import Resolver, build_model, validate_value
from runtime import Engine, Runtime


def fixture(name):
    return json.loads((FIXTURES / name).read_text(encoding="utf-8"))


def session():
    # 固定字节序列，仅为上游已公开测试向量，不是账号凭据。
    return dict(userId="test-user", cUserId="test-user", ssecurity="AQIDBAUGBwgJCgsMDQ4PEA==",
                serviceToken="synthetic-test-session", region="cn")


def record(kind="light"):
    return dict(id="device1", cloudDid="device1", splitSiid=None, name="测试设备", model="test." + kind,
                online=True, homes=["home1"], rooms=["home1:room1"], specType="urn:miot-spec-v2:device:" + kind + ":1:test:1")


def specification(kind="light", name="on", form="bool", access=None, bounds=None, options=None):
    prop = dict(iid=1, type="urn:miot-spec-v2:property:" + name + ":1:test:1", format=form,
                access=access if access is not None else ["read", "write"])
    if bounds:
        prop["value-range"] = bounds
    if options:
        prop["value-list"] = options
    return dict(type="urn:miot-spec-v2:device:" + kind + ":1:test:1", services=[dict(iid=2,
        type="urn:miot-spec-v2:service:" + kind + ":1:test:1", properties=[prop], actions=[])])


class CloudTests(unittest.TestCase):
    def test_upstream_fixed_signing_vector_and_response(self):
        fields, key = signed_params("/home/device_list", {"getVirtualModel": True, "getHuamiDevices": 1},
                                    session()["ssecurity"], "oLHC0+T1BhcBuoFA")
        self.assertEqual(base64.b64encode(key).decode(), "stUjEBQF1H9IaiRGtgWr8jmxbmJs3Xg11OVxJJfJqUI=")
        self.assertEqual(fields["data"], "zQrgJvr34Xp4b97kO7lmB2GNcU1GhZGWLZUbXNz8fHxrLnNVOUaIiX1/Sl4=")
        self.assertEqual(fields["rc4_hash__"], "nV/pEbnR6lBuW/HKPIJmKEjkG21Mw8aVN5gdFQ==")
        self.assertEqual(fields["signature"], "p4PJO1KloBVLQOZqU31Nl4ZPq4s=")
        self.assertEqual(rc4(rc4("中文结果".encode(), key), key).decode(), "中文结果")
        wire = base64.b64encode(rc4(b'{"code":0,"result":{"ok":true}}', key))
        transport = Mock(return_value=Response(200, {}, wire))
        # 每次 nonce 随机；响应加密向量通过固定 nonce 的独立传输验证。
        from unittest.mock import patch
        with patch("cloud.signed_params", return_value=(fields, key)):
            self.assertEqual(Client(session(), transport).request("home/device_list", {}), {"ok": True})

    def test_auth_rate_limit_and_malformed_errors_are_stable(self):
        for response, expected in [(Response(401, {}, b""), "session_expired"),
                                   (Response(429, {}, b""), "rate_limited"),
                                   (Response(200, {}, b'{"code":2}'), "session_expired"),
                                   (Response(200, {}, b'{"code":-1}'), "api_error")]:
            with self.subTest(expected=expected), self.assertRaises(CloudError) as caught:
                Client(session(), Mock(return_value=response)).request("x", {})
            self.assertEqual(caught.exception.code, expected)

    def test_contract_homes_devices_and_split_bindings(self):
        homes = parse_homes(fixture("mi_home_rooms_merged.json")["result"])
        self.assertEqual(len({h["id"] for h in homes}), len(homes))
        self.assertFalse(homes[0]["shared"])
        self.assertTrue(any(h["shared"] for h in homes))
        self.assertEqual(homes[0]["name"], "Contract Home")
        devices = [parse_device(d) for d in fixture("mi_device_list_page.json")["result"]["list"]]
        split = next(d for d in devices if d and d["splitSiid"])
        self.assertEqual((split["cloudDid"], split["splitSiid"]), ("did-switch-0002", 2))
        self.assertIsNone(devices[-1])

    def test_pagination_dedup_and_shared_device_completion(self):
        client = Client(session())
        calls = []
        def request(path, args):
            calls.append((path, args))
            if "gethome" in path:
                return {"homelist": [{"id": "h", "roomlist": [{"id": "r", "dids": ["1", "shared"]}]}]}
            if args.get("dids"):
                return {"list": [{"did": "shared", "model": "test.shared", "isOnline": True}], "has_more": False}
            if not args.get("start_did"):
                return {"list": [{"did": "1", "model": "test.one"}], "has_more": True, "next_start_did": "1"}
            return {"list": [{"did": "1", "model": "test.one"}], "has_more": False}
        client.request = request
        devices, homes = client.discover()
        self.assertEqual({d["id"] for d in devices}, {"1", "shared"})
        self.assertTrue(all(d["homes"] == ["h"] for d in devices))
        self.assertTrue(any(args.get("dids") == ["shared"] for _, args in calls))

    def test_read_chunking_and_write_receipts(self):
        client = Client(session())
        client.request = Mock(return_value=[])
        client.read([{"did": "d", "siid": 2, "piid": i} for i in range(201)])
        self.assertEqual([len(c.args[1]["params"]) for c in client.request.call_args_list], [100, 100, 1])
        for raw in (None, [], [{}], [{"code": -1}]):
            client.request.return_value = raw
            with self.assertRaises(CloudError):
                client.write({"did": "d", "siid": 2, "piid": 1}, True)

    def test_login_urls_redirects_cookie_expiry_and_timeout(self):
        headers = email.message.Message()
        headers.add_header("Set-Cookie", "other=ignored; Path=/")
        headers.add_header("Set-Cookie", "serviceToken=synthetic; Max-Age=3600; Path=/; Secure")
        redirect = email.message.Message()
        redirect["Location"] = "/final"
        transport = Mock(side_effect=[Response(302, redirect, b""), Response(200, headers, b"")])
        token, expiry = Login(transport).exchange("https://sts.api.io.mi.com/start")
        self.assertEqual(token, "synthetic")
        self.assertGreater(expiry, time.time())
        untrusted = email.message.Message()
        untrusted["Location"] = "https://evil.example/"
        with self.assertRaises(CloudError):
            Login(Mock(return_value=Response(302, untrusted, b""))).exchange("https://sts.api.io.mi.com/start")
        with self.assertRaises(CloudError) as expired:
            Login(clock=lambda: 10).poll({"deadline": 9})
        self.assertEqual(expired.exception.code, "qr_expired")
        self.assertIsNone(Login(Mock(side_effect=CloudError("network_error")), clock=lambda: 0).poll({"deadline": 20, "poll": "https://account.xiaomi.com/poll"}))


class SpecTests(unittest.TestCase):
    def test_seven_category_cards_and_generic(self):
        for device_type, kind in (("light", "light"), ("switch", "switch"), ("air-conditioner", "climate"),
                                  ("fan", "fan"), ("curtain", "cover"), ("air-purifier", "air"),
                                  ("humidifier", "air"), ("sensor", "sensor"), ("dishwasher", "generic")):
            with self.subTest(device_type=device_type):
                model = build_model(record(device_type), specification(device_type))
                self.assertEqual(model["kind"], kind)
                self.assertEqual(model["support"], "ready")
                self.assertEqual(model["controls"][0]["confirm"], kind == "generic" or device_type == "sensor")

    def test_contract_spec_range_enum_and_invalid_writes(self):
        model = build_model(record(), fixture("miot_spec_light.json"))
        self.assertTrue(any(c["name"] == "brightness" for c in model["controls"]))
        control = build_model(record(), specification("light", "brightness", "uint8", bounds=[1, 255, 2]))["controls"][0]
        self.assertEqual(validate_value(control, 255), 255)
        for bad in (0, 2, 256, 1.5, True, "3", float("nan")):
            with self.assertRaises(CloudError):
                validate_value(control, bad)
        enum = build_model(record(), specification("light", "mode", "uint8", options=[{"value": 1, "description": "A"}]))["controls"][0]
        with self.assertRaises(CloudError):
            validate_value(enum, 2)

    def test_lock_private_and_missing_specs_never_get_unsafe_controls(self):
        self.assertFalse(build_model(record("lock"), specification("lock"))["controls"][0]["writable"])
        private = specification()
        private["services"][0]["type"] = "urn:miot-spec-v2:service:device-custom:1:test:1"
        self.assertEqual(build_model(record(), private)["controls"], [])
        self.assertEqual(build_model(record(), specification("light", "raw-json", "string"))["controls"], [])
        self.assertEqual(build_model(record(), None)["support"], "no_spec")
        split = record("switch")
        split.update(cloudDid="parent", splitSiid=3)
        spec = specification("switch")
        spec["services"].append(dict(spec["services"][0], iid=3))
        self.assertEqual([c["siid"] for c in build_model(split, spec)["controls"]], [3])

    def test_resolver_index_cache_and_no_guessed_properties(self):
        spec = fixture("miot_spec_light.json")
        index = {"instances": [{"model": "test.light", "type": spec["type"], "version": 2}]}
        transport = Mock(side_effect=[Response(200, {}, json.dumps(index).encode()), Response(200, {}, json.dumps(spec).encode())])
        resolver = Resolver(transport)
        device = dict(record(), specType=None)
        self.assertEqual(resolver.resolve(device), spec)
        self.assertEqual(resolver.resolve(device), spec)
        self.assertEqual(transport.call_count, 2)
        self.assertIsNone(resolver.resolve(dict(device, model="missing")))

    def test_contract_index_chooses_latest_and_partial_results_keep_good_values(self):
        index = fixture("miot_instance_index.json")
        urn = next(i["type"] for i in index["instances"] if i.get("version") == 3)
        spec = fixture("miot_spec_light.json")
        spec["type"] = urn
        transport = Mock(side_effect=[Response(200, {}, json.dumps(index).encode()), Response(200, {}, json.dumps(spec).encode())])
        resolver = Resolver(transport)
        device = dict(record(), id="did-light-0001", cloudDid="did-light-0001", model="contract.light.ceiling", specType=None)
        model = build_model(device, resolver.resolve(device))
        engine = Engine()
        engine.devices = {device["id"]: model}
        keys = [{"did": device["cloudDid"], "siid": c["siid"], "piid": c["piid"]} for c in model["controls"]]
        engine.apply_read(keys, fixture("mi_props_get.json")["result"] + [{"did":"bad","siid":[],"piid":1}])
        self.assertTrue(next(c for c in model["controls"] if c["name"] == "on")["value"])
        self.assertTrue(any(c["updatedAt"] > 0 for c in model["controls"]))


class Host:
    def __init__(self):
        self.value = None
        self.writes = []
        self.fail = False
    def read_secret(self, key):
        return {"success": self.value is not None, "value": self.value, "errorCode": "not_found"}
    def write_secret(self, key, value):
        self.writes.append(threading.current_thread())
        if not self.fail:
            self.value = value
        return {"success": not self.fail}
    def remove_secret(self, key):
        if not self.fail:
            self.value = None
        return {"success": not self.fail}


class RuntimeTests(unittest.TestCase):
    def engine_with_device(self):
        engine = Engine()
        engine.client = Mock()
        engine.devices = {"device1": build_model(record(), specification())}
        engine.status = "ready"
        return engine

    def test_write_readback_timeout_no_replay_and_partial_read_failure(self):
        engine = self.engine_with_device()
        engine.client.write.side_effect = CloudError("network_error")
        engine.client.read.return_value = [{"did": "device1", "siid": 2, "piid": 1, "code": 0, "value": True}]
        engine.control("write", {"deviceId": "device1", "propertyId": "2.1", "value": True}, "op")
        self.assertEqual(engine.client.write.call_count, 1)
        self.assertEqual(engine.operations[-1]["state"], "success")
        engine.client.read.return_value = []
        engine.control("write", {"deviceId": "device1", "propertyId": "2.1", "value": False}, "op2")
        self.assertEqual(engine.operations[-1]["state"], "unconfirmed")
        self.assertTrue(engine.devices["device1"]["controls"][0]["value"])
        engine.control("write", {"deviceId": "device1", "propertyId": "2.1", "value": "bad"}, "op3")
        self.assertEqual(engine.operations[-1]["error"], "invalid_argument")

    def test_generic_confirmation_and_pin_staleness(self):
        engine = self.engine_with_device()
        engine.devices["device1"] = build_model(record("dishwasher"), specification("dishwasher"))
        engine.control("write", {"deviceId": "device1", "propertyId": "2.1", "value": True}, "op")
        self.assertEqual(engine.operations[-1]["error"], "confirmation_required")
        engine.client.write.assert_not_called()
        engine.view["pin"] = {"deviceId": "device1", "propertyId": "2.1"}
        engine.publish()
        self.assertTrue(engine.take_snapshot()[1]["pin"]["stale"])
        engine.devices.clear()
        engine.publish()
        self.assertTrue(engine.take_snapshot()[1]["pin"]["missing"])

    def test_cached_cloud_read_does_not_make_offline_device_fresh(self):
        engine = self.engine_with_device()
        engine.devices["device1"]["online"] = False
        key = {"did":"device1","siid":2,"piid":1}
        engine.apply_read([key], [dict(key,code=0,value=True,updateTime=1700000000)])
        engine.view["pin"] = {"deviceId":"device1","propertyId":"2.1"}
        engine.publish()
        pin = engine.take_snapshot()[1]["pin"]
        self.assertFalse(pin["online"])
        self.assertTrue(pin["stale"])
        self.assertEqual(pin["updatedAt"], 1700000000)

    def test_card_cache_and_pending_state_match_details(self):
        engine = self.engine_with_device()
        control = engine.devices["device1"]["controls"][0]
        control.update(value=True, updatedAt=time.time(), error="")
        engine.view["selected"] = "device1"
        runtime = Runtime(Host(), engine)
        runtime.pending[("device1", "2.1")] = "op1"
        engine.publish()
        state = runtime.update()
        card = state["catalog"][0]["preview"][0]
        self.assertEqual(card["value"], state["detail"]["controls"][0]["value"])
        self.assertTrue(card["pending"])
        self.assertFalse(card["stale"])
        self.assertNotIn("cloudDid", state["catalog"][0])
        engine.devices["device1"]["online"] = False
        engine.publish()
        card = runtime.update()["catalog"][0]["preview"][0]
        self.assertTrue(card["value"])
        self.assertTrue(card["stale"])
        self.assertFalse(control["pending"])

    def test_bounded_card_preview_does_not_hide_multiple_power_channels(self):
        engine = self.engine_with_device()
        template = engine.devices["device1"]["controls"][0]
        engine.devices["device1"]["controls"] = [dict(template, id=f"{i}.1") for i in range(2, 14)]
        engine.publish()
        card = engine.take_snapshot()[1]["catalog"][0]
        self.assertEqual(len(card["preview"]), 10)
        self.assertEqual(card["powerCount"], 12)

    def test_unresolved_cards_do_not_delay_readable_selected_device(self):
        engine = self.engine_with_device()
        engine.devices["other"] = dict(record(),id="other",cloudDid="other",support="loading",controls=[],actions=[],kind="light")
        engine.view = {"open":True,"ids":["device1","other"],"selected":"device1","pin":{}}
        engine.next_catalog = engine.clock()+300
        engine.client.read.return_value = []
        engine.resolver.resolve = Mock()
        engine.tick()
        engine.client.read.assert_called_once()
        engine.resolver.resolve.assert_not_called()

    def test_main_thread_storage_and_late_generation_never_restore_login(self):
        host = Host()
        engine = Engine()
        runtime = Runtime(host, engine)
        engine.messages.put((0, "session", session()))
        runtime.update()
        self.assertEqual(host.writes, [threading.current_thread()])
        self.assertIsNotNone(host.value)
        self.assertNotIn("serviceToken", json.dumps(runtime.update()))
        runtime.invoke("logout", [])
        engine.messages.put((0, "session", session()))
        engine.latest = (0, {"status": "ready"})
        runtime.update()
        self.assertIsNone(host.value)
        self.assertEqual(runtime.state["status"], "signed_out")
        self.assertNotEqual(scope_for(session()), scope_for(dict(session(), userId="other")))

    def test_persistence_failure_and_restart_restore(self):
        host = Host()
        host.fail = True
        runtime = Runtime(host)
        runtime.engine.messages.put((0, "session", session()))
        self.assertEqual(runtime.update()["error"], "storage_error")
        self.assertEqual(runtime.state["status"], "signed_out")
        with self.assertRaises(CloudError):
            runtime.invoke("logout", [])
        runtime.engine.publish()
        self.assertEqual(runtime.update()["error"], "storage_error")
        host.fail = False
        host.value = json.dumps(session())
        engine = Engine(client_factory=lambda _: Mock(discover=Mock(return_value=([], []))))
        runtime = Runtime(host, engine)
        try:
            runtime.init({})
            deadline = time.monotonic() + 2
            while runtime.update()["status"] != "ready" and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertEqual(runtime.state["status"], "ready")
        finally:
            engine.close()

    def test_nonblocking_calls_duplicate_prevention_and_cancel_queued_control(self):
        host = Host()
        engine = self.engine_with_device()
        runtime = Runtime(host, engine)
        runtime.state["status"] = "ready"
        args = {"deviceId": "device1", "propertyId": "2.1", "value": True}
        before = time.monotonic()
        runtime.invoke("write_property", [args])
        self.assertLess(time.monotonic() - before, 0.1)
        with self.assertRaises(CloudError):
            runtime.invoke("write_property", [args])
        runtime.invoke("logout", [])
        engine.start()
        time.sleep(0.15)
        engine.close()
        self.assertIsNone(engine.client)
        self.assertEqual(engine.operations, [])

    def test_logout_is_not_blocked_by_full_network_queue(self):
        runtime = Runtime(Host())
        for _ in range(32):
            runtime.engine.submit(0, "view", {})
        self.assertTrue(runtime.invoke("logout", [])["accepted"])
        self.assertEqual(runtime.engine.commands.qsize(), 1)

    def test_cloud_backoff_keeps_old_values_and_auth_expiry_stops_control(self):
        engine = self.engine_with_device()
        engine.client.discover.side_effect = CloudError("rate_limited")
        engine.start()
        time.sleep(0.25)
        engine.close()
        self.assertEqual(engine.error, "rate_limited")
        self.assertEqual(len(engine.devices), 1)
        self.assertGreater(engine.next_catalog, engine.clock())
        engine = self.engine_with_device()
        engine.client.write.side_effect = CloudError("session_expired")
        engine.control("write", {"deviceId": "device1", "propertyId": "2.1", "value": True}, "op")
        self.assertEqual(engine.status, "session_expired")
        self.assertIsNone(engine.client)


class PresentationTests(unittest.TestCase):
    def test_qml_preferences_confirmation_and_rendering(self):
        runner = shutil.which("qmltestrunner")
        cache = ROOT / "build/default/CMakeCache.txt"
        if not runner and cache.is_file():
            match = re.search(r"^Qt6_DIR:PATH=(.+)$", cache.read_text(encoding="utf-8"), re.M)
            if match:
                candidate = Path(match[1].strip()).parents[2] / "bin/qmltestrunner.exe"
                if candidate.is_file():
                    runner = str(candidate)
        if not runner:
            self.skipTest("未找到 Qt qmltestrunner")
        with tempfile.TemporaryDirectory() as directory:
            module = Path(directory) / "XuanHub"
            module.mkdir()
            shutil.copy2(ROOT / "app/qml/PluginPopup.qml", module)
            (module / "qmldir").write_text("module XuanHub\nPluginPopup 1.0 PluginPopup.qml\n", encoding="utf-8")
            log = Path(directory) / "result.txt"
            images = ROOT / "build/default/mijia-qa"
            images.mkdir(exist_ok=True)
            result = subprocess.run([runner, "-import", directory, "-input", str(ROOT / "tests/qml/tst_Mijia.qml"),
                "-o", str(log) + ",txt"], cwd=images,
                env={**os.environ, "QT_QPA_PLATFORM": "offscreen", "QT_QUICK_BACKEND": "software", "QT_QUICK_CONTROLS_STYLE": "FluentWinUI3",
                     "QT_QPA_FONTDIR": str(Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts")},
                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30)
            output = result.stdout + result.stderr + (log.read_text(encoding="utf-8") if log.is_file() else "")
            self.assertEqual(result.returncode, 0, output)
            for error in ("Binding loop", "ReferenceError", "TypeError", "Unable to assign"):
                self.assertNotIn(error, output)


if __name__ == "__main__":
    unittest.main()
