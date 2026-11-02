#!/usr/bin/env python3
"""Verify a signed private OTA manifest against its image, trust and version policy."""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import sys

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

from ota_release_contract import CHIP_FAMILY, PROJECT_NAME, load_trust

REQUIRED_SAFETY = {
    "esp_idf_rollback": True,
    "safe_mode": True,
    "browser_ota_enabled": False,
    "signed_image_verification": True,
}
REQUIRED_UPDATE_POLICY = {
    "downgrade_default": "deny",
    "same_version": "allow",
    "same_project_required": True,
    "same_chip_family_required": True,
}
VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:[-+].*)?$")


def version_core(value: str) -> tuple[int, int, int]:
    """Parse the numeric release core; suffixes such as -dev do not alter order."""
    match = VERSION_RE.fullmatch(value.strip())
    if not match:
        raise RuntimeError(f"unsupported firmware version format: {value!r}")
    return tuple(int(part) for part in match.groups())


def verify_signature(manifest: bytes, signature_path: Path, trust_path: Path) -> str:
    """Verify the detached signature and return the trusted key id."""
    trust, raw_key = load_trust(trust_path)
    envelope = json.loads(signature_path.read_text(encoding="utf-8"))
    if envelope.get("schema") != 1 or envelope.get("algorithm") != "Ed25519":
        raise RuntimeError("unsupported OTA signature envelope")
    if envelope.get("key_id") != trust["key_id"]:
        raise RuntimeError("OTA signature key_id is not trusted")
    digest = hashlib.sha256(manifest).hexdigest()
    if envelope.get("manifest_sha256") != digest:
        raise RuntimeError("manifest SHA256 does not match signature envelope")
    signature = base64.b64decode(
        str(envelope.get("signature_base64", "")), validate=True)
    try:
        Ed25519PublicKey.from_public_bytes(raw_key).verify(signature, manifest)
    except InvalidSignature as exc:
        raise RuntimeError("Ed25519 manifest signature is invalid") from exc
    return str(trust["key_id"])


def contract_errors(data: dict[str, object], image: bytes, key: str) -> list[str]:
    """Return compatibility/integrity errors after signature verification."""
    image_data = data.get("image", {})
    trust_data = data.get("trust", {})
    distribution = data.get("distribution", {})
    checks = (
        (data.get("schema") == 2, "unsupported schema"),
        (data.get("profile") == "private-production", "wrong profile"),
        (data.get("project") == PROJECT_NAME, "project mismatch"),
        (data.get("chip_family") == CHIP_FAMILY, "chip family mismatch"),
        (data.get("safety") == REQUIRED_SAFETY, "safety contract mismatch"),
        (data.get("update_policy") == REQUIRED_UPDATE_POLICY, "update policy mismatch"),
        (trust_data.get("algorithm") == "Ed25519", "trust algorithm mismatch"),
        (trust_data.get("key_id") == key, "manifest trust key mismatch"),
        (trust_data.get("signature_required") is True, "signature is not required"),
        (distribution.get("public") is False, "OTA marked public"),
        (distribution.get("contains_private_profile") is True, "private-profile warning missing"),
        (image_data.get("kind") == "ota", "wrong image kind"),
        (image_data.get("filename") == "firmware.ota.bin", "wrong image filename"),
        (image_data.get("size") == len(image), "image size mismatch"),
        (image_data.get("sha256") == hashlib.sha256(image).hexdigest(), "image SHA256 mismatch"),
    )
    return [message for ok, message in checks if not ok]


def enforce_version_policy(target: str, current: str | None, allow_downgrade: bool) -> None:
    """Reject a signed downgrade unless an explicit recovery override is supplied."""
    target_core = version_core(target)
    if current is None:
        return
    current_core = version_core(current)
    if target_core < current_core and not allow_downgrade:
        raise RuntimeError(
            f"downgrade denied by policy: target {target} < current {current}")


def parse_args() -> argparse.Namespace:
    """Parse verifier inputs, including deployment-time version policy."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("image", type=Path)
    parser.add_argument("signature", type=Path)
    parser.add_argument("trust", type=Path)
    parser.add_argument("--current-version")
    parser.add_argument(
        "--allow-downgrade", action="store_true",
        help="recovery-only override; requires --current-version")
    args = parser.parse_args()
    if args.allow_downgrade and not args.current_version:
        parser.error("--allow-downgrade requires --current-version")
    return args


def validate(args: argparse.Namespace) -> tuple[dict[str, object], str]:
    """Verify signature, exact image contract and downgrade policy."""
    manifest = args.manifest.read_bytes()
    trusted_key = verify_signature(manifest, args.signature, args.trust)
    data = json.loads(manifest.decode("utf-8"))
    image = args.image.read_bytes()
    errors = contract_errors(data, image, trusted_key)
    if errors:
        raise RuntimeError("; ".join(errors))
    enforce_version_policy(
        str(data.get("version", "")), args.current_version, args.allow_downgrade)
    return data, trusted_key


def main() -> int:
    args = parse_args()
    try:
        data, trusted_key = validate(args)
    except (RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(
        f"PRIVATE_OTA_SIGNATURE=PASS version={data.get('version')} "
        f"key_id={trusted_key} sha256={data['image']['sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
