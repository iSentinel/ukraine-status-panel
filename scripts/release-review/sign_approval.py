#!/usr/bin/env python3
"""Sign the reviewed receipt on a separate protected GitHub runner."""

import argparse
import base64
from datetime import datetime, timezone
from hashlib import sha256
import json
import os
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from loguru import logger

from verify_packet import DRAFT_SHA256, VERSION, check_draft, require, write_new


FIELDS = (
    "schema", "release_version", "decision", "receipt_sha256",
    "reviewer_id", "reviewed_at_utc",
)


def signing_key(encoded, trusted_fingerprint):
    """Decode only the protected key and enforce out-of-band key identity."""
    require(bool(encoded and trusted_fingerprint),
            "Missing protected key or trusted public-key fingerprint")
    pem = base64.b64decode(encoded, validate=True)
    key = serialization.load_pem_private_key(pem, password=None)
    require(isinstance(key, Ed25519PrivateKey), "Signing key is not Ed25519")
    public = key.public_key().public_bytes(
        serialization.Encoding.DER,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    actual = sha256(public).hexdigest()
    require(actual == trusted_fingerprint, "Reviewer key identity differs")
    return key


def approve(draft, reviewer, key, now=None):
    """Sign exact canonical fields accepted by the production V2 verifier."""
    check_draft(draft)
    require(isinstance(reviewer, str) and reviewer and
            len(reviewer) <= 128 and reviewer == reviewer.strip(),
            "Verified reviewer identity is missing")
    approval = {
        "schema": "usp-receipt-v2-approval-ed25519-v1",
        "release_version": VERSION,
        "decision": "APPROVE_RECEIPT_V2_PROMOTION",
        "receipt_sha256": DRAFT_SHA256,
        "reviewer_id": reviewer,
        "reviewed_at_utc": (now or datetime.now(timezone.utc)).isoformat(),
    }
    canonical = json.dumps(approval, sort_keys=True,
                           separators=(",", ":")).encode()
    approval["signature_b64"] = base64.b64encode(key.sign(canonical)).decode()
    key.public_key().verify(base64.b64decode(approval["signature_b64"]),
                            canonical)
    return approval


def main():
    """Sign one verified draft only after an independent GitHub approval."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--draft", type=Path, required=True)
    parser.add_argument("--reviewer-json", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    draft = args.draft.read_bytes()
    identity = json.loads(args.reviewer_json.read_text())
    require(identity.get("environment") == "firmware-ed25519-review",
            "Missing protected environment approval")
    reviewer = identity["reviewer_id"]
    key = signing_key(os.getenv("USP_REVIEW_SIGNING_KEY_B64", ""),
                      os.getenv("USP_REVIEW_PUBLIC_KEY_SHA256", ""))
    approval = approve(draft, reviewer, key)
    write_new(args.output, (json.dumps(approval, indent=2) + "\n").encode())
    logger.info("ED25519_APPROVAL=PASS version={} reviewer={} receipt={}",
                VERSION, reviewer, DRAFT_SHA256)


if __name__ == "__main__":
    main()
