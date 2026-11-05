#!/usr/bin/env python3
"""Record a conscious GitHub owner release decision without signing or OTA.

This is a self-approval record, not an independent review or a signature.
It only creates a new JSON artifact after an explicit manual workflow run.
"""

import argparse
from datetime import datetime, timezone
from hashlib import sha256
import json
import os
from pathlib import Path

from loguru import logger

VERSION = "0.115.415-dev"
OWNER = "iSentinel"
DRAFT_SHA256 = "a2412a570f06aeaf6da0cfc50db1cfc6e15cc9288832d4514a21fbab5c1aec4e"
PACKET_SHA256 = "ee07627db447d7cae14cd8ee62dfcd33b84fc4f6dfb81cb0367ffbff21b04fcf"
STATEMENT = "I APPROVE 0.115.415-dev AS OWNER"
REPOSITORY = "iSentinel/ukraine-status-panel"


def require(condition, message):
    """Reject missing consent or an unrecognized GitHub run identity."""
    if not condition:
        raise ValueError(message)


def verified_environment(environ):
    """Bind consent to an actual manual main-branch GitHub owner run."""
    require(environ.get("GITHUB_REPOSITORY") == REPOSITORY,
            "Unexpected repository")
    require(environ.get("GITHUB_REF") == "refs/heads/main",
            "Owner approval is restricted to main")
    require(environ.get("GITHUB_EVENT_NAME") == "workflow_dispatch",
            "Owner approval must be started manually")
    require(environ.get("GITHUB_ACTOR") == OWNER,
            "The repository owner must start the workflow")
    require(environ.get("GITHUB_TRIGGERING_ACTOR") == OWNER,
            "The owner must also initiate any rerun")


def approved_record(args, environ):
    """Build a strict local owner declaration bound to frozen .415 hashes."""
    verified_environment(environ)
    require(args.version == VERSION, "Unexpected release version")
    require(args.receipt_sha256 == DRAFT_SHA256, "Original receipt differs")
    require(args.packet_sha256 == PACKET_SHA256, "Original packet differs")
    require(args.statement == STATEMENT, "Explicit owner consent is missing")
    return {
        "schema": "usp-receipt-v2-owner-approval-v1",
        "approval_mode": "owner",
        "release_version": VERSION,
        "decision": "APPROVE_RECEIPT_V2_PROMOTION",
        "owner_login": OWNER,
        "receipt_sha256": DRAFT_SHA256,
        "packet_sha256": PACKET_SHA256,
        "reviewed_proof_files": 24,
        "independently_signed": False,
        "review_method": "OWNER_DECLARATION_NOT_INDEPENDENT",
        "approved_at_utc": datetime.now(timezone.utc).isoformat(),
    }


def arguments():
    """Read explicit typed values from the manual workflow dispatch."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--receipt-sha256", required=True)
    parser.add_argument("--packet-sha256", required=True)
    parser.add_argument("--statement", required=True)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def main():
    """Export one owner-declared record and its SHA256, never private keys."""
    args = arguments()
    try:
        record = approved_record(args, os.environ)
        data = (json.dumps(record, indent=2) + "\n").encode()
        flags = os.O_CREAT | os.O_EXCL | os.O_WRONLY
        with os.fdopen(os.open(args.output, flags, 0o600), "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
    except (ValueError, OSError, TypeError) as error:
        logger.error("GITHUB_OWNER_APPROVAL=BLOCKED {}", error)
        raise SystemExit(2) from error
    logger.info("GITHUB_OWNER_APPROVAL=PASS release={} sha256={} signed=false",
                VERSION, sha256(data).hexdigest())


if __name__ == "__main__":
    main()
