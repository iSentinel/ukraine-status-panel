#!/usr/bin/env python3
"""Create a detached Ed25519 signature for a private OTA manifest."""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from ota_release_contract import key_id, load_trust


def load_private_key(path: Path) -> Ed25519PrivateKey:
    """Load the local release key and reject other key types."""
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise RuntimeError("OTA signing key must be Ed25519")
    return key


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("private_key", type=Path)
    parser.add_argument("trust", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    trust, trusted_raw = load_trust(args.trust)
    private_key = load_private_key(args.private_key)
    raw = private_key.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    if raw != trusted_raw or key_id(raw) != trust["key_id"]:
        raise RuntimeError("private signing key does not match the trusted release key")
    payload = args.manifest.read_bytes()
    envelope = {
        "schema": 1,
        "algorithm": "Ed25519",
        "key_id": trust["key_id"],
        "manifest_sha256": hashlib.sha256(payload).hexdigest(),
        "signature_base64": base64.b64encode(private_key.sign(payload)).decode(),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(envelope, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(args.output)


if __name__ == "__main__":
    main()
