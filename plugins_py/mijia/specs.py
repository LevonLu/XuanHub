# specs.py — MIoT 规格解析、类型卡片和受校验的属性绑定
from __future__ import annotations

import json
import math
import time
import urllib.parse
from decimal import Decimal

from cloud import CloudError, Transport

KINDS = {"light": "light", "outlet": "switch", "switch": "switch",
         "air-conditioner": "climate", "air-condition-outlet": "climate",
         "fan": "fan", "curtain": "cover", "air-purifier": "air",
         "humidifier": "air", "air-fresh": "air", "sensor": "sensor",
         "temperature-humidity-sensor": "sensor", "magnet-sensor": "sensor",
         "motion-sensor": "sensor", "occupancy-sensor": "sensor",
         "water-leak-sensor": "sensor", "air-monitor": "sensor", "lock": "sensor"}
PRIMARY_SERVICES = {
    "light": {"light"}, "switch": {"switch", "outlet"},
    "climate": {"air-conditioner"}, "fan": {"fan"}, "cover": {"curtain"},
    "air": {"air-purifier", "humidifier", "air-fresh", "fan"},
    "sensor": {"environment", "battery", "magnet-sensor", "motion-sensor", "lock"}}
PRIMARY_PROPERTIES = {"on", "brightness", "color-temperature", "mode", "target-temperature",
    "temperature", "relative-humidity", "target-humidity", "fan-level", "speed-level",
    "horizontal-swing", "vertical-swing", "motor-control", "target-position", "current-position",
    "pm2.5-density", "air-quality", "filter-life-level", "battery-level", "electric-power",
    "illumination", "contact-state", "motion-state", "occupancy-status", "water-leakage-state", "door-state"}
NOISE = {"device-information", "device-custom", "exception-card", "ota", "firmware-update", "custom"}
BLOCKED = {"reset", "factory-reset", "reboot", "bind-timestamp", "trigger-status", "code",
           "password", "token", "key", "unlock", "remote-unlock", "firmware-version", "serial-number"}
NUMBERS = {"uint8", "uint16", "uint32", "uint64", "int8", "int16", "int32", "int64", "float", "double"}
UNITS = {"celsius": "°C", "percentage": "%", "kelvin": "K", "watt": "W", "lux": "lx",
         "microgram-per-cubic-meter": "µg/m³", "ppm": "ppm", "volt": "V", "ampere": "A"}


def short(urn) -> str:
    parts = str(urn or "").split(":")
    return parts[3] if len(parts) > 3 else str(urn or "")


def finite(value) -> bool:
    return type(value) in (int, float) and math.isfinite(value)


def kind_for(record: dict) -> str:
    return KINDS.get(short(record.get("specType")), "generic")


def build_model(record: dict, spec: dict | None) -> dict:
    model = dict(record, kind=kind_for(record), controls=[], actions=[], support="no_spec")
    if not spec:
        return model
    kind = KINDS.get(short(spec.get("type")), "generic")
    model["kind"] = kind
    locked = short(spec.get("type")) in {"lock", "door-lock", "safe", "door"}
    for service in spec.get("services", []):
        if not isinstance(service, dict) or type(service.get("iid")) is not int:
            continue
        siid = service["iid"]
        if record.get("splitSiid") is not None and siid != record["splitSiid"]:
            continue
        sname = short(service.get("type"))
        if sname in NOISE or sname.endswith("-custom"):
            continue
        for prop in service.get("properties", []):
            if not isinstance(prop, dict) or type(prop.get("iid")) is not int:
                continue
            name = short(prop.get("type"))
            if (name in BLOCKED or name.endswith("-custom") or "password" in name or "token" in name
                or name.startswith(("reset-", "factory-", "reboot-", "bind-", "unbind-", "firmware-", "unlock-"))):
                continue
            access = prop.get("access", [])
            if not isinstance(access, list):
                continue
            readable = "read" in access
            form = prop.get("format")
            if form != "bool" and form not in NUMBERS:
                continue
            options = [dict(value=item["value"], label=str(item.get("description", item["value"])))
                       for item in prop.get("value-list", [])
                       if isinstance(item, dict) and finite(item.get("value"))]
            bounds = prop.get("value-range", [])
            valid_range = (isinstance(bounds, list) and len(bounds) == 3 and all(finite(v) for v in bounds)
                           and bounds[0] <= bounds[1] and bounds[2] > 0)
            editor = "bool" if form == "bool" else "enum" if options else "number" if valid_range else "readout"
            writable = not locked and "write" in access and editor != "readout"
            if not readable and not writable:
                continue
            primary = kind != "generic" and name in PRIMARY_PROPERTIES and (
                sname in PRIMARY_SERVICES.get(kind, set()) or sname in {"environment", "battery", "power-consumption", "filter"})
            model["controls"].append({"id": f'{siid}.{prop["iid"]}', "siid": siid, "piid": prop["iid"],
                "name": name, "service": sname, "description": str(prop.get("description", name)),
                "format": form, "editor": editor, "readable": readable, "writable": writable,
                "primary": primary, "confirm": writable and not primary,
                "unit": UNITS.get(prop.get("unit"), str(prop.get("unit") or "")),
                "options": options, "range": bounds if valid_range else [],
                "value": None, "updatedAt": 0, "error": "not_read", "pending": False})
        # 仅接受已知窗帘的无入参开/停/关；通用动作不因零入参而自动放行。
        if kind == "cover" and sname == "curtain":
            for action in service.get("actions", []):
                if not isinstance(action, dict) or type(action.get("iid")) is not int or action.get("in"):
                    continue
                name = short(action.get("type"))
                if name in {"open", "close", "stop"}:
                    model["actions"].append({"id": f'{siid}.{action["iid"]}', "siid": siid,
                                             "aiid": action["iid"], "name": name})
    model["controls"].sort(key=lambda c: (not c["primary"], c["siid"], c["piid"]))
    model["support"] = "ready" if model["controls"] or model["actions"] else "no_properties"
    return model


def validate_value(control: dict, value):
    if not control["writable"]:
        raise CloudError("read_only")
    editor = control["editor"]
    if editor == "bool":
        if type(value) is not bool:
            raise CloudError("invalid_argument")
    elif not finite(value):
        raise CloudError("invalid_argument")
    elif editor == "enum":
        if value not in [o["value"] for o in control["options"]]:
            raise CloudError("invalid_argument")
    elif editor == "number":
        minimum, maximum, step = map(lambda v: Decimal(str(v)), control["range"])
        decimal = Decimal(str(value))
        if not minimum <= decimal <= maximum or (decimal - minimum) % step != 0:
            raise CloudError("invalid_argument")
    else:
        raise CloudError("read_only")
    if control["format"].startswith(("int", "uint")) and (type(value) is not int and int(value) != value):
        raise CloudError("invalid_argument")
    return value


class Resolver:
    def __init__(self, transport=None):
        self.transport = transport or Transport()
        self.specs = {}
        self.models = {}
        self.index = None
        self.failed = {}

    def _get(self, url):
        response = self.transport(url)
        if response.status == 429:
            raise CloudError("rate_limited")
        if response.status != 200:
            raise CloudError("network_error")
        try:
            raw = json.loads(response.body)
            if not isinstance(raw, dict):
                raise ValueError
            return raw
        except (ValueError, UnicodeError):
            raise CloudError("invalid_response") from None

    def resolve(self, record: dict) -> dict | None:
        model = record["model"]
        urn = record.get("specType") or self.models.get(model)
        if self.failed.get(model, 0) > time.monotonic():
            return None
        if not urn:
            if self.index is None:
                raw = self._get("https://miot-spec.org/miot-spec-v2/instances?status=released")
                self.index = {}
                for item in raw.get("instances", []):
                    if not isinstance(item, dict) or not isinstance(item.get("model"), str) or not isinstance(item.get("type"), str):
                        continue
                    old = self.index.get(item["model"], {})
                    if int(item.get("version", 0)) >= int(old.get("version", -1)):
                        self.index[item["model"]] = item
            urn = self.index.get(model, {}).get("type")
        if not urn:
            self.failed[model] = time.monotonic() + 300
            return None
        self.models[model] = urn
        if urn not in self.specs:
            raw = self._get("https://miot-spec.org/miot-spec-v2/instance?type=" + urllib.parse.quote(urn, safe=""))
            if raw.get("type") != urn or not isinstance(raw.get("services"), list):
                raise CloudError("invalid_response")
            self.specs[urn] = raw
        return self.specs[urn]
