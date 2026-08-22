# plugin_signing.py — 生成插件发布者密钥并创建确定性 ECDSA P-256 签名
from __future__ import annotations

import base64
import hashlib
import hmac
import json
import os
from pathlib import Path
import secrets
import tempfile


ALGORITHM = "ecdsa-p256-sha256"
SIGNED_TARGET = "manifest.sha256"
_P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
_A = _P - 3
_B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
_N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
_G = (
    0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
    0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5,
)


class SigningError(ValueError):
    pass


def _point_add(left: tuple[int, int] | None,
               right: tuple[int, int] | None) -> tuple[int, int] | None:
    if left is None:
        return right
    if right is None:
        return left
    x1, y1 = left
    x2, y2 = right
    if x1 == x2 and (y1 + y2) % _P == 0:
        return None
    if left == right:
        slope = (3 * x1 * x1 + _A) * pow(2 * y1, -1, _P) % _P
    else:
        slope = (y2 - y1) * pow(x2 - x1, -1, _P) % _P
    x3 = (slope * slope - x1 - x2) % _P
    return x3, (slope * (x1 - x3) - y1) % _P


def _scalar_multiply(scalar: int,
                     point: tuple[int, int] = _G) -> tuple[int, int] | None:
    result = None
    addend = point
    while scalar:
        if scalar & 1:
            result = _point_add(result, addend)
        addend = _point_add(addend, addend)
        scalar >>= 1
    return result


def _canonical_base64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def _decode_base64(value: object, field: str, expected_size: int) -> bytes:
    if not isinstance(value, str) or not value:
        raise SigningError(f"signing key field '{field}' must be Base64 text")
    try:
        decoded = base64.b64decode(value, validate=True)
    except (ValueError, TypeError) as error:
        raise SigningError(f"signing key field '{field}' is not canonical Base64") from error
    if _canonical_base64(decoded) != value or len(decoded) != expected_size:
        raise SigningError(
            f"signing key field '{field}' must contain {expected_size} bytes")
    return decoded


def public_key_for_private(private_key: bytes) -> bytes:
    if len(private_key) != 32:
        raise SigningError("P-256 private key must contain 32 bytes")
    scalar = int.from_bytes(private_key, "big")
    if not 1 <= scalar < _N:
        raise SigningError("P-256 private key scalar is outside the valid range")
    point = _scalar_multiply(scalar)
    if point is None:
        raise SigningError("cannot derive P-256 public key")
    return b"\x04" + point[0].to_bytes(32, "big") + point[1].to_bytes(32, "big")


def key_id(public_key: bytes) -> str:
    if len(public_key) != 65 or public_key[0] != 4:
        raise SigningError("public key must be an uncompressed P-256 point")
    return "sha256:" + hashlib.sha256(public_key).hexdigest()


def _deterministic_nonce(private_scalar: int, digest: bytes) -> int:
    # RFC 6979 §3.2（q 长度与 SHA-256 输出均为 256 bit）。
    private_octets = private_scalar.to_bytes(32, "big")
    reduced_digest = (int.from_bytes(digest, "big") % _N).to_bytes(32, "big")
    value = b"\x01" * 32
    key = b"\x00" * 32
    key = hmac.new(key, value + b"\x00" + private_octets + reduced_digest,
                   hashlib.sha256).digest()
    value = hmac.new(key, value, hashlib.sha256).digest()
    key = hmac.new(key, value + b"\x01" + private_octets + reduced_digest,
                   hashlib.sha256).digest()
    value = hmac.new(key, value, hashlib.sha256).digest()
    while True:
        value = hmac.new(key, value, hashlib.sha256).digest()
        candidate = int.from_bytes(value, "big")
        if 1 <= candidate < _N:
            return candidate
        key = hmac.new(key, value + b"\x00", hashlib.sha256).digest()
        value = hmac.new(key, value, hashlib.sha256).digest()


def sign_manifest(private_key: bytes, manifest_bytes: bytes) -> bytes:
    scalar = int.from_bytes(private_key, "big")
    if len(private_key) != 32 or not 1 <= scalar < _N:
        raise SigningError("P-256 private key is invalid")
    digest = hashlib.sha256(manifest_bytes).digest()
    nonce = _deterministic_nonce(scalar, digest)
    point = _scalar_multiply(nonce)
    if point is None:
        raise SigningError("cannot create ECDSA signature")
    r = point[0] % _N
    s = (pow(nonce, -1, _N)
         * (int.from_bytes(digest, "big") + r * scalar)) % _N
    if r == 0 or s == 0:
        raise SigningError("ECDSA produced an invalid zero component")
    # 统一 low-S，减少同一签名的可塑形式；Windows CNG 可直接验证 r || s。
    if s > _N // 2:
        s = _N - s
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def verify_manifest(public_key: bytes, manifest_bytes: bytes,
                    signature: bytes) -> bool:
    if len(public_key) != 65 or public_key[0] != 4 or len(signature) != 64:
        return False
    point = (int.from_bytes(public_key[1:33], "big"),
             int.from_bytes(public_key[33:], "big"))
    if not (0 <= point[0] < _P and 0 <= point[1] < _P
            and (point[1] * point[1] - point[0] ** 3
                 - _A * point[0] - _B) % _P == 0):
        return False
    r = int.from_bytes(signature[:32], "big")
    s = int.from_bytes(signature[32:], "big")
    if not (1 <= r < _N and 1 <= s < _N):
        return False
    digest = hashlib.sha256(manifest_bytes).digest()
    inverse = pow(s, -1, _N)
    u1 = int.from_bytes(digest, "big") * inverse % _N
    u2 = r * inverse % _N
    result = _point_add(_scalar_multiply(u1), _scalar_multiply(u2, point))
    return result is not None and result[0] % _N == r


def generate_key_document(publisher: str = "") -> dict[str, object]:
    scalar = secrets.randbelow(_N - 1) + 1
    private_key = scalar.to_bytes(32, "big")
    public_key = public_key_for_private(private_key)
    document: dict[str, object] = {
        "schemaVersion": 1,
        "algorithm": ALGORITHM,
        "keyId": key_id(public_key),
        "privateKey": _canonical_base64(private_key),
        "publicKey": _canonical_base64(public_key),
    }
    if publisher.strip():
        document["publisher"] = publisher.strip()
    return document


def write_key_file(path: Path, publisher: str = "") -> dict[str, object]:
    path = path.expanduser().resolve(strict=False)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        raise SigningError(f"refusing to overwrite existing signing key: {path}")
    content = (json.dumps(generate_key_document(publisher), ensure_ascii=False,
                          indent=2, sort_keys=True) + "\n").encode("utf-8")
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        try:
            os.chmod(temporary_path, 0o600)
        except OSError:
            pass
        os.replace(temporary_path, path)
    finally:
        temporary_path.unlink(missing_ok=True)
    return json.loads(content)


def load_key_file(path: Path) -> dict[str, object]:
    try:
        document = json.loads(path.expanduser().read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise SigningError(f"cannot read signing key: {error}") from error
    if not isinstance(document, dict) or document.get("schemaVersion") != 1:
        raise SigningError("unsupported signing key schemaVersion")
    if document.get("algorithm") != ALGORITHM:
        raise SigningError("unsupported signing key algorithm")
    private_key = _decode_base64(document.get("privateKey"), "privateKey", 32)
    public_key = _decode_base64(document.get("publicKey"), "publicKey", 65)
    if public_key_for_private(private_key) != public_key:
        raise SigningError("signing key publicKey does not match privateKey")
    calculated_id = key_id(public_key)
    if document.get("keyId") != calculated_id:
        raise SigningError("signing key keyId does not match publicKey")
    return {
        "privateKey": private_key,
        "publicKey": public_key,
        "keyId": calculated_id,
        "publisher": str(document.get("publisher", "")).strip(),
    }


def create_signature(key: dict[str, object], manifest_bytes: bytes
                     ) -> tuple[dict[str, str], dict[str, object]]:
    private_key = key["privateKey"]
    public_key = key["publicKey"]
    if not isinstance(private_key, bytes) or not isinstance(public_key, bytes):
        raise SigningError("loaded signing key has invalid binary fields")
    signature = sign_manifest(private_key, manifest_bytes)
    if not verify_manifest(public_key, manifest_bytes, signature):
        raise SigningError("self-verification of generated signature failed")
    identifier = str(key["keyId"])
    declaration = {"algorithm": ALGORITHM, "keyId": identifier}
    signature_document: dict[str, object] = {
        "schemaVersion": 1,
        "algorithm": ALGORITHM,
        "keyId": identifier,
        "publicKey": _canonical_base64(public_key),
        "signed": SIGNED_TARGET,
        "signature": _canonical_base64(signature),
    }
    return declaration, signature_document
