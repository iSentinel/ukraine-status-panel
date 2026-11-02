#!/usr/bin/env python3
"""Validate the repository OTA trust-anchor policy and signing-key isolation."""
from __future__ import annotations

import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TRUST = ROOT / "security/ota-release-trust.json"
PRIVATE = ".work-state/ota-signing/release-ed25519-private.pem"


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    data = json.loads(TRUST.read_text(encoding="utf-8"))
    raw = base64.b64decode(data["public_key_base64"], validate=True)
    expected = "ed25519:" + hashlib.sha256(raw).hexdigest()[:16]
    need(len(raw) == 32, "trust anchor is not a raw Ed25519 public key")
    need(data.get("algorithm") == "Ed25519", "wrong trust algorithm")
    need(data.get("key_id") == expected, "trust key_id/fingerprint mismatch")
    tracked = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "--error-unmatch", PRIVATE],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    need(not tracked, "private OTA signing key must never be tracked")
    gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8")
    need(".work-state/" in gitignore, "private work-state directory is not ignored")
    scan = (ROOT / "scripts/secret-scan.sh").read_text(encoding="utf-8")
    need("PRIVATE KEY" in scan, "tracked private-key scan missing")
    print(f"OTA_SIGNATURE_STATIC=PASS key_id={expected}")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"OTA_SIGNATURE_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
