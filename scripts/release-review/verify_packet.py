#!/usr/bin/env python3
"""Verify the frozen .415 hardware review packet without executing its files."""

import argparse
from hashlib import sha256
from io import BytesIO
import json
import os
from pathlib import Path, PurePosixPath
import re
import tarfile
from urllib.parse import urlparse
from urllib.request import Request, urlopen

from loguru import logger

PACKET_SHA256 = "ee07627db447d7cae14cd8ee62dfcd33b84fc4f6dfb81cb0367ffbff21b04fcf"
DRAFT_SHA256 = "a2412a570f06aeaf6da0cfc50db1cfc6e15cc9288832d4514a21fbab5c1aec4e"
VERSION = "0.115.415-dev"
ROOT = "usp-v2-independent-review/"
METADATA = {
    "README.md", "RELEASE_V2_SIGNED_ACCEPTANCE.md",
    "independent-review-request.md", "receipt-v2-sign-offline.py",
    "reconciliation-receipt-draft.json",
}
MAX_COMPRESSED = 25 * 1024 * 1024
MAX_EXPANDED = 100 * 1024 * 1024


def require(condition, message):
    """Reject incomplete or untrusted review data."""
    if not condition:
        raise ValueError(message)


def download_packet(url):
    """Download a size-bounded HTTPS packet from a protected input."""
    require(urlparse(url).scheme == "https", "Review packet requires HTTPS")
    request = Request(url, headers={"User-Agent": "USP-Release-Reviewer/1"})
    try:
        with urlopen(request, timeout=45) as response:
            require(urlparse(response.geturl()).scheme == "https",
                    "Review packet redirect must use HTTPS")
            packet = response.read(MAX_COMPRESSED + 1)
    except (OSError, ValueError) as error:
        raise ValueError("Protected packet download failed") from None
    require(0 < len(packet) <= MAX_COMPRESSED, "Unsafe packet size")
    return packet


def safe_entry(member):
    """Accept only small ordinary files under the expected archive root."""
    path = PurePosixPath(member.name)
    require(member.isfile(), "Review packet contains a non-file")
    require(member.name.startswith(ROOT), "Unexpected packet root")
    require(not path.is_absolute() and ".." not in path.parts,
            "Unsafe evidence path")
    require(0 <= member.size <= 50 * 1024 * 1024,
            "Oversized review file")


def read_members(packet):
    """Inspect exact archive member names and their bytes without extraction."""
    entries = {}
    expanded = 0
    with tarfile.open(fileobj=BytesIO(packet), mode="r:gz") as archive:
        members = archive.getmembers()
        require(len(members) == 29, "Expected exactly 29 review files")
        for member in members:
            safe_entry(member)
            require(member.name not in entries, "Duplicate review file")
            expanded += member.size
            require(expanded <= MAX_EXPANDED, "Oversized review archive")
            with archive.extractfile(member) as stream:
                data = stream.read(member.size + 1)
            require(len(data) == member.size, "Truncated review file")
            entries[member.name.removeprefix(ROOT)] = data
    return entries


def check_draft(draft_blob):
    """Require the exact frozen receipt and expected proof contract."""
    require(sha256(draft_blob).hexdigest() == DRAFT_SHA256,
            "Review receipt hash differs from the frozen candidate")
    draft = json.loads(draft_blob)
    require(draft.get("schema") == "usp-hardware-readonly-proof-v2-draft",
            "Unexpected receipt schema")
    require(draft.get("status") == "PRESEAL_DRAFT_NOT_ACCEPTED",
            "Receipt was already promoted")
    require(draft.get("release_version") == VERSION,
            "Wrong firmware version")
    proofs = draft.get("proof_sha256")
    require(isinstance(proofs, dict) and len(proofs) == 24,
            "Expected 24 proof hashes")
    return proofs


def check_evidence(entries, proofs):
    """Hash every original evidence file and reject unexpected additions."""
    expected = set(METADATA)
    for name, digest in proofs.items():
        path = PurePosixPath(name)
        require(isinstance(name, str) and name and
                not path.is_absolute() and ".." not in path.parts,
                "Unsafe proof name")
        require(isinstance(digest, str) and
                re.fullmatch(r"[0-9a-f]{64}", digest) is not None,
                "Invalid proof SHA256")
        item = "evidence/" + name
        require(item in entries, f"Missing original evidence: {name}")
        require(sha256(entries[item]).hexdigest() == digest,
                f"Changed original evidence: {name}")
        expected.add(item)
    require(set(entries) == expected, "Unexpected review packet entries")


def verify_packet(packet):
    """Verify packet, original receipt and all 24 unmodified proof files."""
    require(sha256(packet).hexdigest() == PACKET_SHA256,
            "Packet SHA256 differs from the frozen review kit")
    entries = read_members(packet)
    draft = entries.get("reconciliation-receipt-draft.json")
    require(draft is not None, "Original receipt is missing")
    check_evidence(entries, check_draft(draft))
    return draft


def write_new(path, blob):
    """Create an owner-only review file without overwriting any old file."""
    path = Path(path)
    require(not path.is_symlink(), "Unsafe review output path")
    with os.fdopen(os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600),
                   "wb") as output:
        output.write(blob)
        output.flush()
        os.fsync(output.fileno())


def main():
    """Download and inspect the genuine .415 packet after GitHub approval."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packet-url-env", default="USP_REVIEW_PACKET_URL")
    parser.add_argument("--output-draft", type=Path, required=True)
    args = parser.parse_args()
    url = os.environ.get(args.packet_url_env, "")
    require(url, "Protected packet URL is not configured")
    draft = verify_packet(download_packet(url))
    write_new(args.output_draft, draft)
    logger.info("REVIEW_PACKET=PASS evidence=24 version={} draft_sha256={}",
                VERSION, DRAFT_SHA256)


if __name__ == "__main__":
    main()
