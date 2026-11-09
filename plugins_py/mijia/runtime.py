# runtime.py — 后台网络调度与主线程密钥交接，结果按会话代次隔离
from __future__ import annotations

import copy
import itertools
import json
import queue
import threading
import time

from cloud import Client, CloudError, Login, scope_for, validate_session
from specs import Resolver, build_model, finite, kind_for, validate_value


class Engine:
    def __init__(self, client_factory=Client, login=None, resolver=None, clock=time.monotonic):
        self.client_factory = client_factory
        self.login, self.resolver = login or Login(), resolver or Resolver()
        self.clock = clock
        self.commands, self.messages = queue.PriorityQueue(maxsize=32), queue.Queue(maxsize=16)
        self.counter = itertools.count()
        self.closed, self.lock = threading.Event(), threading.Lock()
        self.latest, self.thread = None, None
        self.generation, self.desired_generation, self.interval = 0, 0, 10
        self.reset(0)

    def start(self):
        self.thread = threading.Thread(target=self.run, name="mijia-network", daemon=True)
        self.thread.start()

    def submit(self, generation, action, args=None, operation=""):
        if action == "reset":
            # 主线程立即使排队任务失效，不等旧网络请求返回后才取消。
            with self.lock:
                self.desired_generation = generation
            while True:
                try:
                    self.commands.get_nowait()
                except queue.Empty:
                    break
        priority = 0 if action in {"reset", "activate", "write", "action"} else 1
        try:
            self.commands.put_nowait((priority, next(self.counter), generation, action, args, operation))
        except queue.Full:
            raise CloudError("busy") from None

    def close(self):
        self.closed.set()
        if self.thread:
            self.thread.join(timeout=0.3)

    def reset(self, generation):
        self.generation = generation
        self.client = self.ticket = None
        self.scope, self.status, self.error = "", "signed_out", ""
        self.devices, self.homes, self.operations = {}, [], []
        self.catalog_loaded = False
        self.view = {"open": False, "ids": [], "selected": "", "pin": {}}
        self.read_keys = []
        self.retry = 5
        self.next_read = self.next_catalog = self.next_poll = 0
        self.resolver.failed.clear()

    def publish(self):
        catalog = [{k: d[k] for k in ("id", "name", "model", "online", "homes", "rooms", "kind", "support")}
                   for d in self.devices.values()]
        # 列表仅携带卡片摘要，避免把所有设备的完整规格反复送入 IPC；写入仍校验原始规格。
        preview_names = {"on", "temperature", "relative-humidity", "target-temperature", "brightness",
                         "electric-power", "current-position", "pm2.5-density", "fan-level", "battery-level",
                         "contact-state", "door-state", "mode", "air-quality", "filter-life-level",
                         "illumination", "motion-state", "occupancy-status", "water-leakage-state"}
        for card in catalog:
            source = self.devices[card["id"]]
            card["powerCount"] = sum(c["name"] == "on" and c["editor"] == "bool" for c in source["controls"])
            fields = [c for c in source["controls"] if c["name"] in preview_names][:10]
            card["preview"] = [{k: c[k] for k in ("id", "name", "value", "unit", "editor", "options", "range",
                "readable", "writable", "confirm", "pending", "error", "updatedAt")} for c in fields]
            for field in card["preview"]:
                field["stale"] = bool(field["error"]) or not card["online"] or time.time() - field["updatedAt"] > 30
        pin = self.view["pin"]
        device = self.devices.get(pin.get("deviceId"))
        control = next((c for c in device["controls"] if c["id"] == pin.get("propertyId")), None) if device else None
        pinned = dict(control, deviceId=device["id"], deviceName=device["name"], online=device["online"]) if control else {}
        if pin.get("deviceId") and not pinned:
            if device and device["support"] in {"loading", "spec_error"}:
                pinned = {"loading": True, "deviceId": pin["deviceId"], "deviceName": device["name"],
                          "error": "not_read", "online": device["online"], "updatedAt": 0}
            else:
                pinned = {"missing": True, "deviceId": pin["deviceId"]}
        if pinned and not pinned.get("missing"):
            pinned["stale"] = bool(pinned["error"]) or not pinned["online"] or time.time() - pinned["updatedAt"] > self.interval * 3
        detail = copy.deepcopy(self.devices.get(self.view["selected"], {}))
        for field in detail.get("controls", []):
            field["stale"] = bool(field["error"]) or not detail["online"] or time.time() - field["updatedAt"] > 30
        snapshot = {"status": self.status, "error": self.error, "accountScope": self.scope,
            "catalogLoaded": self.catalog_loaded,
            "catalog": catalog, "homes": [{"id": h["id"], "name": h["name"], "shared": h["shared"],
                "rooms": [{"id": r["id"], "name": r["name"]} for r in h["rooms"]]} for h in self.homes],
            "detail": detail, "pin": pinned,
            "operations": self.operations[-32:], "onlineCount": sum(d["online"] for d in self.devices.values()),
            "qrImage": self.ticket["image"] if self.ticket else "",
            "qrExpiresAt": self.ticket["deadline"] if self.ticket else 0}
        with self.lock:
            self.latest = (self.generation, copy.deepcopy(snapshot))

    def take_snapshot(self):
        with self.lock:
            result, self.latest = self.latest, None
            return result

    def run(self):
        while not self.closed.is_set():
            try:
                command = self.commands.get(timeout=0.1)
            except queue.Empty:
                command = None
            try:
                if command:
                    _, _, generation, action, args, operation = command
                    if generation != self.desired_generation:
                        continue
                    if action == "reset" and generation >= self.generation:
                        self.reset(generation)
                    if generation != self.generation:
                        continue
                    self.handle(action, args, operation)
                    self.publish()
                elif self.ticket or self.client:
                    self.tick()
            except Exception as error:
                # 不打印底层异常、URL 或响应；网络库异常可能含登录票据。
                self.error = error.code if isinstance(error, CloudError) else "invalid_response"
                if self.error == "session_expired":
                    self.client, self.status = None, "session_expired"
                elif self.ticket or self.status == "login_loading":
                    self.ticket, self.status = None, "signed_out"
                self.next_catalog = max(self.next_catalog, self.clock() + self.retry)
                self.next_read = self.clock() + self.retry
                self.retry = min(300, self.retry * 2)
                self.read_keys = []
                self.publish()

    def handle(self, action, args, operation):
        if action == "login":
            self.status = "login_loading"
            self.publish()
            self.ticket = self.login.start(args)
            self.status = "waiting_scan"
        elif action == "activate":
            self.client = self.client_factory(args)
            self.scope = scope_for(args)
            self.status, self.error = "ready", ""
        elif action == "config":
            self.interval, self.next_read = args, 0
        elif action == "view":
            self.view, self.read_keys, self.next_read = args, [], 0
        elif action == "refresh":
            self.next_catalog = self.next_read = 0
            self.resolver.failed.clear()
            for device in self.devices.values():
                if device["support"] in {"no_spec", "spec_error"}:
                    device.update(support="loading", specRetry=0)
        elif action in {"write", "action"}:
            self.control(action, args, operation)

    def tick(self):
        now = self.clock()
        if self.ticket:
            if now < self.next_poll:
                return
            self.next_poll = now + 1
            session = self.login.poll(self.ticket)
            if session:
                self.messages.put_nowait((self.generation, "session", session))
                self.ticket, self.status = None, "saving_session"
                self.publish()
            return
        if not self.client:
            return
        if now >= self.next_catalog:
            records, homes = self.client.discover()
            fresh = {}
            for record in records:
                old = self.devices.get(record["id"])
                if old and old["model"] == record["model"] and old.get("specType") == record.get("specType"):
                    fresh[record["id"]] = dict(old, **record)
                else:
                    fresh[record["id"]] = dict(record, kind=kind_for(record), support="loading", controls=[], actions=[])
            self.devices, self.homes = fresh, homes
            self.catalog_loaded = True
            self.next_catalog, self.error = now + 300, ""
            self.publish()
            return
        ids = list(dict.fromkeys([self.view["selected"], self.view["pin"].get("deviceId", "")]
                                + (self.view["ids"] if self.view["open"] else [])))
        # 当前详情和固定读数优先；其他卡片的规格不能阻塞已可读设备。
        for did in (self.view["selected"], self.view["pin"].get("deviceId", "")):
            if self.resolve_device(did, now):
                return
        if not self.read_keys and now >= self.next_read:
            keys = {}
            for did in ids:
                device = self.devices.get(did)
                if not device:
                    continue
                for control in device["controls"]:
                    wanted = self.view["open"] or (did == self.view["pin"].get("deviceId")
                                                   and control["id"] == self.view["pin"].get("propertyId"))
                    if control["readable"] and wanted:
                        key = {"did": device["cloudDid"], "siid": control["siid"], "piid": control["piid"]}
                        keys[(key["did"], key["siid"], key["piid"])] = key
            self.read_keys = list(keys.values())
            self.next_read = now + (5 if self.view["open"] else self.interval)
        if self.read_keys:
            chunk, self.read_keys = self.read_keys[:100], self.read_keys[100:]
            try:
                results = self.client.read(chunk)
            except CloudError:
                self.apply_read(chunk, [])
                raise
            self.apply_read(chunk, results)
            self.error, self.retry = "", 5
            self.publish()
            return
        for did in ids:
            if self.resolve_device(did, now):
                return

    def resolve_device(self, did, now):
        device = self.devices.get(did)
        if not device or device["support"] not in {"loading", "spec_error"} or now < device.get("specRetry", 0):
            return False
        try:
            self.devices[did] = build_model(device, self.resolver.resolve(device))
        except CloudError:
            device["support"], device["specRetry"] = "spec_error", now + 60
        self.publish()
        return True

    def apply_read(self, keys, results):
        returned = {(str(r.get("did")), r["siid"], r["piid"]): r for r in results
                    if isinstance(r, dict) and isinstance(r.get("did"), (str, int))
                    and type(r.get("siid")) is int and type(r.get("piid")) is int}
        requested = {(k["did"], k["siid"], k["piid"]) for k in keys}
        for device in self.devices.values():
            for control in device["controls"]:
                key = (device["cloudDid"], control["siid"], control["piid"])
                if key not in requested:
                    continue
                raw = returned.get(key, {})
                value = raw.get("value")
                valid = type(value) is bool if control["format"] == "bool" else finite(value)
                if raw.get("code") == 0 and "value" in raw and valid:
                    timestamp = raw.get("updateTime")
                    if not finite(timestamp) or timestamp <= 0:
                        timestamp = time.time()
                    elif timestamp > 100_000_000_000:
                        timestamp /= 1000
                    # 云端可能返回睡眠/离线设备的缓存，成功读取不等于设备在线。
                    control.update(value=value, updatedAt=min(timestamp, time.time()), error="")
                else:
                    control["error"] = "device_error"

    def control(self, action, args, operation):
        result = {"id": operation, "state": "pending", "error": "", "deviceId": args["deviceId"],
                  "propertyId": args.get("propertyId", args.get("actionId", ""))}
        self.operations.append(result)
        self.operations = self.operations[-32:]
        control = None
        try:
            if not self.client:
                raise CloudError("not_signed_in")
            device = self.devices.get(args["deviceId"])
            if not device or not device["online"]:
                raise CloudError("device_offline")
            if action == "write":
                control = next((c for c in device["controls"] if c["id"] == args.get("propertyId")), None)
                if not control:
                    raise CloudError("invalid_argument")
                value = validate_value(control, args.get("value"))
                if control["confirm"] and args.get("confirmed") is not True:
                    raise CloudError("confirmation_required")
                control["pending"] = True
                self.publish()
                key = {"did": device["cloudDid"], "siid": control["siid"], "piid": control["piid"]}
                try:
                    self.client.write(key, value)
                except CloudError as error:
                    if error.code != "network_error":
                        raise
                if control["readable"]:
                    try:
                        self.apply_read([key], self.client.read([key]))
                        if control["error"] or control["value"] != value:
                            result["state"] = "unconfirmed"
                    except CloudError as error:
                        if error.code == "session_expired":
                            raise
                        control["error"], result["state"] = "device_error", "unconfirmed"
                else:
                    result["state"] = "unconfirmed"
            else:
                binding = next((a for a in device["actions"] if a["id"] == args.get("actionId")), None)
                if not binding:
                    raise CloudError("invalid_argument")
                try:
                    self.client.action(device["cloudDid"], binding["siid"], binding["aiid"])
                except CloudError as error:
                    if error.code != "network_error":
                        raise
                result["state"] = "unconfirmed"
            if result["state"] == "pending":
                result["state"] = "success"
            self.next_read = 0
        except Exception as error:
            result.update(state="failed", error=error.code if isinstance(error, CloudError) else "invalid_response")
            if result["error"] == "session_expired":
                self.client, self.status = None, "session_expired"
        finally:
            if control:
                control["pending"] = False


class Runtime:
    def __init__(self, host, engine=None):
        self.host, self.engine = host, engine or Engine()
        self.generation, self.counter = 0, itertools.count(1)
        self.pending = {}
        self.local_error = ""
        self.state = self.empty_state()
        self.available = all(callable(getattr(host, name, None)) for name in ("read_secret", "write_secret", "remove_secret"))

    @staticmethod
    def empty_state():
        return {"status": "signed_out", "error": "", "accountScope": "", "catalogLoaded": False, "catalog": [], "homes": [],
                "detail": {}, "pin": {}, "qrImage": "", "operations": [], "onlineCount": 0}

    def init(self, config):
        if not self.available:
            self.state.update(status="host_upgrade_required", error="host_upgrade_required")
            return
        self.engine.start()
        self.configure(config)
        stored = self.host.read_secret("login_session")
        if stored["success"]:
            try:
                session = validate_session(json.loads(stored["value"]))
                if session.get("expiresAt") and session["expiresAt"] <= time.time():
                    self.state.update(status="session_expired")
                else:
                    self.engine.submit(self.generation, "activate", session)
                    self.state.update(status="restoring")
            except (CloudError, ValueError, TypeError):
                self.state.update(error="invalid_session")
        elif stored.get("errorCode") != "not_found":
            code = "host_upgrade_required" if stored.get("errorCode") == "unsupported_service" else "storage_error"
            self.state.update(error=code)
            if code == "host_upgrade_required":
                self.available = False
                self.state["status"] = code

    def configure(self, config):
        interval = config.get("refreshSeconds", 10)
        if type(interval) is not int or not 5 <= interval <= 300:
            raise CloudError("invalid_argument")
        if self.available:
            self.engine.submit(self.generation, "config", interval)

    def update(self):
        if not self.available:
            return copy.deepcopy(self.state)
        latest = self.engine.take_snapshot()
        if latest and latest[0] == self.generation:
            self.state = latest[1]
            completed = {op["id"] for op in self.state["operations"] if op["state"] != "pending"}
            self.pending = {key: op for key, op in self.pending.items() if op not in completed}
        while True:
            try:
                generation, kind, session = self.engine.messages.get_nowait()
            except queue.Empty:
                break
            if generation != self.generation or kind != "session":
                continue
            try:
                encoded = json.dumps(validate_session(session), ensure_ascii=False, separators=(",", ":"))
                if not self.host.write_secret("login_session", encoded)["success"]:
                    raise CloudError("storage_error")
                self.local_error = ""
                self.engine.submit(generation, "activate", session)
                self.state.update(status="restoring", qrImage="")
            except (CloudError, ValueError, TypeError):
                self.local_error = "storage_error"
                self.state.update(status="signed_out", error="storage_error", qrImage="")
        state = copy.deepcopy(self.state)
        if self.local_error:
            state["error"] = self.local_error
        for control in state["detail"].get("controls", []):
            control["pending"] = control["pending"] or (state["detail"]["id"], control["id"]) in self.pending
        for device in state["catalog"]:
            for control in device.get("preview", []):
                control["pending"] = control["pending"] or (device["id"], control["id"]) in self.pending
        return state

    def invoke(self, action, args):
        if not self.available:
            raise CloudError("host_upgrade_required")
        if action in {"start_login", "cancel_login", "logout"}:
            if action == "start_login" and (len(args) != 1 or args[0] not in ("cn", "sg", "de", "us", "ru")):
                raise CloudError("invalid_argument")
            self.generation += 1
            self.engine.submit(self.generation, "reset")
            self.pending.clear()
            self.local_error = ""
            self.state = self.empty_state()
            if action == "logout":
                if not self.host.remove_secret("login_session")["success"]:
                    self.local_error = "storage_error"
                    self.state["error"] = "storage_error"
                    raise CloudError("storage_error")
            elif action == "start_login":
                self.state["status"] = "login_loading"
                self.engine.submit(self.generation, "login", args[0])
            return {"accepted": True}
        if action == "set_view":
            if len(args) != 1 or not isinstance(args[0], dict):
                raise CloudError("invalid_argument")
            raw = args[0]
            ids, pin = raw.get("ids", []), raw.get("pin", {})
            if not isinstance(ids, list) or not all(isinstance(v, str) for v in ids) or not isinstance(pin, dict):
                raise CloudError("invalid_argument")
            clean_pin = {k: v for k, v in pin.items() if k in {"deviceId", "propertyId"} and isinstance(v, str)}
            self.engine.submit(self.generation, "view", {"open": raw.get("open") is True, "ids": ids[:40],
                "selected": str(raw.get("selected", "")), "pin": clean_pin})
            return {"accepted": True}
        if self.state["status"] != "ready":
            raise CloudError("not_signed_in")
        if action == "refresh":
            self.engine.submit(self.generation, "refresh")
            return {"accepted": True}
        if action in {"write_property", "curtain_action"}:
            if len(args) != 1 or not isinstance(args[0], dict) or not isinstance(args[0].get("deviceId"), str):
                raise CloudError("invalid_argument")
            target = args[0].get("propertyId" if action == "write_property" else "actionId")
            if not isinstance(target, str) or not target or len(target) > 128:
                raise CloudError("invalid_argument")
            operation = str(next(self.counter))
            key = (args[0]["deviceId"], args[0].get("propertyId", args[0].get("actionId", "")))
            if key in self.pending:
                raise CloudError("busy")
            self.engine.submit(self.generation, "write" if action == "write_property" else "action", args[0], operation)
            self.pending[key] = operation
            return {"accepted": True, "operationId": operation}
        raise CloudError("unsupported_action")
