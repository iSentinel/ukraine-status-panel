#!/usr/bin/env python3
"""Guard the device-side Ed25519 verifier foundation against scope creep."""
from __future__ import annotations

import base64
import json
from pathlib import Path
import re
import sys

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

ROOT = Path(__file__).resolve().parents[1]
CPP_PATH = ROOT / "components/usp_ota_guard/ota_guard.cpp"
H_PATH = ROOT / "components/usp_ota_guard/ota_guard.h"
PY_PATH = ROOT / "components/usp_ota_guard/__init__.py"
GENERATOR = ROOT / "scripts/generate-public-first-flash.py"
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")
TRUST = json.loads((ROOT / "security/ota-release-trust.json").read_text(encoding="utf-8"))


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def array_bytes(source: str, symbol: str) -> bytes:
    match = re.search(
        rf"{symbol}\s*\{{\{{(?P<body>.*?)\}}\}};", source, re.S)
    need(match is not None, f"missing C++ byte array {symbol}")
    values = re.findall(r"0x([0-9a-fA-F]{2})", match.group("body"))
    return bytes(int(value, 16) for value in values)


def main() -> None:
    cpp = CPP_PATH.read_text(encoding="utf-8")
    header = H_PATH.read_text(encoding="utf-8")
    py = PY_PATH.read_text(encoding="utf-8")
    generator = GENERATOR.read_text(encoding="utf-8")
    trusted = base64.b64decode(TRUST["public_key_base64"], validate=True)
    embedded = array_bytes(cpp, "TRUST_PUBLIC_KEY")
    signature = array_bytes(cpp, "SELF_TEST_SIGNATURE")
    message_match = re.search(
        r'SELF_TEST_MESSAGE\[\]\s*=\s*\n?\s*"([^"]+)";', cpp)
    need(message_match is not None, "self-test message missing")
    message = message_match.group(1).encode()

    need(embedded == trusted, "embedded public key differs from trust anchor")
    need(TRUST["key_id"] in cpp, "embedded key id differs from trust anchor")
    need(len(signature) == 64, "Ed25519 self-test signature must be 64 bytes")
    Ed25519PublicKey.from_public_bytes(trusted).verify(signature, message)

    need("ge25519_double_scalarmult_vartime" in cpp and "sc25519_reduce" in cpp,
         "device Ed25519 ref10 verification missing")
    need("mbedtls_sha512_update" in cpp and "crypto_verify_32" in cpp,
         "device Ed25519 SHA-512/final comparison missing")
    need("crypto_sign_verify_detached" not in cpp,
         "unsupported full libsodium sign API must not be referenced")
    need("sodium_init" in cpp, "libsodium initialization missing")
    need("randombytes_set_implementation" in cpp,
         "ESP32-safe libsodium randombytes backend registration missing")
    need(cpp.find("randombytes_set_implementation") < cpp.find("sodium_init()"),
         "randombytes backend must be registered before sodium_init")
    need("version_allowed" in header and "parse_version_core_" in cpp,
         "device downgrade policy helper missing")
    need('cg.add_library("esphome/libsodium", "1.10021.11")' in py,
         "libsodium dependency is not pinned")
    need("strip_marked_blocks(text, \"PRIVATE_UPDATE\")" in generator,
         "public generator does not strip private update blocks")
    need("# PRIVATE_UPDATE_BEGIN: ota-guard-component" in PROD,
         "production component is not marked private-update")
    need("usp_ota_guard:" in PROD and "id: ota_guard" in PROD,
         "production verifier instance missing")
    need("web_ota_device_verifier" in PROD, "read-only verifier status missing")
    need("usp_ota_guard" not in PUBLIC and "web_ota_device_verifier" not in PUBLIC,
         "public first-flash leaked device verifier")

    forbidden = (
        "esp_ota_begin", "esp_ota_write", "esp_ota_end",
        "esp_ota_set_boot_partition", "esp_restart", "App.safe_reboot",
        '"/update"', "ota_backend_",
    )
    combined = cpp + header
    leaked = [token for token in forbidden if token in combined]
    need(not leaked, f"verifier foundation contains flash/reboot actions: {leaked}")
    print(f"OTA_DEVICE_VERIFIER_STATIC=PASS key_id={TRUST['key_id']}")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, ValueError) as exc:
        print(f"OTA_DEVICE_VERIFIER_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
