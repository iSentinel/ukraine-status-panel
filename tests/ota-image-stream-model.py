#!/usr/bin/env python3
"""Exercise the streaming OTA image contract across chunk boundaries."""
from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import tempfile


def fixture() -> bytes:
    blob = bytearray(8193)
    blob[0] = 0xE9
    struct.pack_into("<I", blob, 0x20, 0xABCD5432)
    blob[0x30:0x30 + len(b"2026.9.1")] = b"2026.9.1"
    blob[0x50:0x50 + len(b"ukraine-status-panel")] = b"ukraine-status-panel"
    for i in range(0x80, len(blob)):
        blob[i] = (i * 17 + 3) & 0xFF
    return bytes(blob)


def stream(blob: bytes, chunks: list[int], expected_size: int, expected_sha: str) -> tuple[bool, str]:
    sha = hashlib.sha256()
    prefix = bytearray()
    seen = 0
    pos = 0
    for size in chunks:
        if pos >= len(blob):
            break
        piece = blob[pos:pos + size]
        if seen + len(piece) > expected_size:
            return False, "size_overflow"
        sha.update(piece)
        if len(prefix) < 128:
            prefix.extend(piece[:128 - len(prefix)])
        seen += len(piece)
        pos += len(piece)
    if pos < len(blob):
        piece = blob[pos:]
        if seen + len(piece) > expected_size:
            return False, "size_overflow"
        sha.update(piece)
        prefix.extend(piece[:max(0, 128 - len(prefix))])
        seen += len(piece)
    if seen != expected_size:
        return False, "size_mismatch"
    if len(prefix) < 0x70 or prefix[0] != 0xE9 or struct.unpack_from("<I", prefix, 0x20)[0] != 0xABCD5432:
        return False, "header_mismatch"
    framework = bytes(prefix[0x30:0x50]).split(b"\0", 1)[0]
    project = bytes(prefix[0x50:0x70]).split(b"\0", 1)[0]
    if framework != b"2026.9.1" or project != b"ukraine-status-panel":
        return False, "app_desc_mismatch"
    if sha.hexdigest() != expected_sha:
        return False, "sha256_mismatch"
    return True, "ok"


def main() -> None:
    blob = fixture()
    digest = hashlib.sha256(blob).hexdigest()
    for chunks in ([1] * 140, [31, 7, 1, 89, 4096], [127, 1, 128, 777], [4096, 4096, 1]):
        assert stream(blob, list(chunks), len(blob), digest) == (True, "ok")
    assert stream(blob[:-1], [73, 211], len(blob), digest)[1] == "size_mismatch"
    assert stream(blob + b"x", [4096], len(blob), digest)[1] == "size_overflow"
    bad = bytearray(blob); bad[-1] ^= 1
    assert stream(bytes(bad), [17, 111, 2048], len(blob), digest)[1] == "sha256_mismatch"
    bad = bytearray(blob); bad[0] = 0
    assert stream(bytes(bad), [1, 31, 96], len(blob), hashlib.sha256(bad).hexdigest())[1] == "header_mismatch"
    bad = bytearray(blob); bad[0x50] ^= 1
    assert stream(bytes(bad), [37, 91], len(blob), hashlib.sha256(bad).hexdigest())[1] == "app_desc_mismatch"
    print("OTA_IMAGE_STREAM_MODEL=PASS chunk_cases=4 negative_cases=5")


if __name__ == "__main__":
    main()
