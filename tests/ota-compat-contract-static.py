#!/usr/bin/env python3
"""Exercise the signed private OTA compatibility contract without hardware."""
from __future__ import annotations

import base64
import hashlib
import json
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import struct
import subprocess
import sys
import tempfile

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "scripts/generate-private-ota-manifest.py"
SIGNER = ROOT / "scripts/sign-private-ota-manifest.py"
VALIDATOR = ROOT / "scripts/validate-private-ota-manifest.py"
BUNDLER = ROOT / "scripts/make-private-ota-bundle.sh"
PROD = ROOT / "firmware/ukraine-status-panel.yaml"
PUBLIC = ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml"
TRUST = ROOT / "security/ota-release-trust.json"


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def scalar(text: str, key: str) -> str:
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith(f"{key}:"):
            return stripped.split(":", 1)[1].strip().strip('"')
    raise AssertionError(f"missing scalar: {key}")


def fixture(path: Path, device: str, project: str, version: str) -> None:
    blob = bytearray(4096)
    blob[0] = 0xE9
    struct.pack_into("<I", blob, 0x20, 0xABCD5432)
    blob[0x30:0x30 + len(b"2026.9.1")] = b"2026.9.1"
    device_bytes = device.encode()
    blob[0x50:0x50 + len(device_bytes)] = device_bytes
    marker = (project + "\0" + version).encode()
    blob[0x180:0x180 + len(marker)] = marker
    path.write_bytes(blob)


def make_keypair(tmp: Path) -> tuple[Path, Path]:
    key = Ed25519PrivateKey.generate()
    private_path = tmp / "private.pem"
    trust_path = tmp / "trust.json"
    private_path.write_bytes(key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption()))
    raw = key.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    key_id = "ed25519:" + hashlib.sha256(raw).hexdigest()[:16]
    trust = {
        "schema": 1,
        "algorithm": "Ed25519",
        "key_id": key_id,
        "public_key_base64": base64.b64encode(raw).decode(),
        "purpose": "test",
    }
    trust_path.write_text(json.dumps(trust) + "\n", encoding="utf-8")
    return private_path, trust_path


def expect_failure(command: list[str], needle: str) -> None:
    proc = subprocess.run(command, text=True, capture_output=True)
    need(proc.returncode != 0, f"command unexpectedly passed: {command}")
    output = (proc.stdout + proc.stderr).lower()
    need(needle.lower() in output, f"failure did not mention {needle!r}: {output}")


def main() -> None:
    text = PROD.read_text(encoding="utf-8")
    need(CURRENT_FIRMWARE_MARKER in text, "wrong production version")
    need("enable_ota_rollback: true" in text, "rollback prerequisite missing")
    need("ota: false" in text, "browser OTA must remain disabled")
    for path in (GENERATOR, SIGNER, VALIDATOR, BUNDLER, TRUST):
        need(path.exists(), f"OTA contract asset missing: {path.name}")
    version = scalar(text, "firmware_version")
    device = scalar(text, "device_name")
    project = "ukraine-status-panel.panel"

    with tempfile.TemporaryDirectory(prefix="usp-ota-contract-") as temp:
        tmp = Path(temp)
        private_key, trust = make_keypair(tmp)
        image = tmp / "firmware.ota.bin"
        manifest = tmp / "ota-compatibility.json"
        signature = tmp / "ota-compatibility.sig.json"
        fixture(image, device, project, version)
        subprocess.run(
            [sys.executable, str(GENERATOR), str(PROD), str(image), str(trust), str(manifest)],
            check=True, capture_output=True, text=True)
        subprocess.run(
            [sys.executable, str(SIGNER), str(manifest), str(private_key), str(trust), str(signature)],
            check=True, capture_output=True, text=True)
        data = json.loads(manifest.read_text(encoding="utf-8"))
        need(data["schema"] == 2, "signed OTA manifest schema missing")
        need(data["trust"]["signature_required"] is True, "signature must be required")
        need(data["safety"]["signed_image_verification"] is True,
             "signed image verification contract missing")
        need(data["update_policy"]["downgrade_default"] == "deny",
             "downgrade-deny policy missing")
        base = [sys.executable, str(VALIDATOR), str(manifest), str(image),
                str(signature), str(trust)]
        subprocess.run(base, check=True, capture_output=True, text=True)
        core = version.split("-", 1)[0].split(".")
        need(len(core) == 3, "unexpected test version format")
        older = f"{core[0]}.{core[1]}.{max(0, int(core[2]) - 1)}-dev"
        newer = f"{core[0]}.{core[1]}.{int(core[2]) + 1}-dev"
        subprocess.run(
            base + ["--current-version", older],
            check=True, capture_output=True, text=True)
        subprocess.run(
            base + ["--current-version", version],
            check=True, capture_output=True, text=True)
        expect_failure(
            base + ["--current-version", newer],
            "downgrade denied")
        subprocess.run(
            base + ["--current-version", newer, "--allow-downgrade"],
            check=True, capture_output=True, text=True)
        expect_failure(
            base + ["--current-version", "not-a-version"],
            "unsupported firmware version format")

        original = image.read_bytes()
        image.write_bytes(original[:-1] + bytes([original[-1] ^ 0x01]))
        expect_failure(
            [sys.executable, str(VALIDATOR), str(manifest), str(image), str(signature), str(trust)],
            "sha256")
        image.write_bytes(original)

        tampered = json.loads(manifest.read_text(encoding="utf-8"))
        tampered["version"] = "9.9.9-tampered"
        manifest.write_text(json.dumps(tampered, indent=2) + "\n", encoding="utf-8")
        expect_failure(
            [sys.executable, str(VALIDATOR), str(manifest), str(image), str(signature), str(trust)],
            "manifest sha256")

        fixture(tmp / "wrong.bin", "other-device", project, version)
        expect_failure(
            [sys.executable, str(GENERATOR), str(PROD), str(tmp / "wrong.bin"), str(trust), str(tmp / "wrong.json")],
            "descriptor project mismatch")
        expect_failure(
            [sys.executable, str(GENERATOR), str(PUBLIC), str(image), str(trust), str(tmp / "public.json")],
            "public first-flash")

    for workflow in (ROOT / ".github/workflows/pages.yml", ROOT / ".github/workflows/release.yml"):
        body = workflow.read_text(encoding="utf-8")
        for token in ("make-private-ota-bundle", "generate-private-ota-manifest", "sign-private-ota-manifest"):
            need(token not in body, f"private OTA leaked into public workflow: {workflow.name}")
    bundler = BUNDLER.read_text(encoding="utf-8")
    need("USP_OTA_SIGNING_KEY" in bundler and "chmod 600" in bundler,
         "private signing/bundle guard missing")
    print("OTA_COMPAT_CONTRACT_STATIC=PASS")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"OTA_COMPAT_CONTRACT_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
