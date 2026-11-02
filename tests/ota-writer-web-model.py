#!/usr/bin/env python3
"""Model the one-time permit and dry-run writer Web stream semantics."""
from __future__ import annotations

import hashlib
from dataclasses import dataclass


@dataclass
class Model:
    expected: bytes
    session: str = "session"
    permit: str | None = "permit"
    permit_deadline: int = 30_000
    now: int = 0
    state: str = "idle"
    failure: str = "none"
    seen: int = 0
    data: bytes = b""

    def request(self, *, session: str, offset: int, final: bool, payload: bytes,
                permit: str | None = None) -> int:
        if session != self.session:
            return 403
        writer_new = self.state == "idle"
        writer_active = self.state in {"prepared", "writing"}
        if not (writer_new or writer_active) or not payload:
            return 400
        end = offset + len(payload)
        expected_len = len(self.expected)
        valid_shape = (
            offset == self.seen
            and end <= expected_len
            and (end == expected_len if final else end < expected_len)
        )
        if not valid_shape:
            if writer_active:
                self.state = "failed"
                if offset != self.seen:
                    self.failure = "offset_mismatch"
                elif end > expected_len:
                    self.failure = "size_overflow"
                else:
                    self.failure = "invalid_chunk"
            return 400
        if writer_new:
            if self.now >= self.permit_deadline or self.permit is None:
                self.permit = None
                return 409
            if permit != self.permit:
                return 403
            self.permit = None
            self.state = "prepared"
        self.data += payload
        self.seen += len(payload)
        self.state = "writing"
        if not final:
            return 202
        if self.seen != expected_len:
            self.state = "failed"
            self.failure = "size_mismatch"
            return 422
        if hashlib.sha256(self.data).digest() != hashlib.sha256(self.expected).digest():
            self.state = "failed"
            self.failure = "sha256_mismatch"
            return 422
        self.state = "finalized"
        return 200


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    expected = b"abcdefgh"

    m = Model(expected)
    need(m.request(session="wrong", offset=0, final=False, payload=b"abcd", permit="permit") == 403,
         "wrong session accepted")
    need(m.permit == "permit" and m.state == "idle" and m.seen == 0,
         "wrong session mutated writer/permit")

    m = Model(expected)
    need(m.request(session="session", offset=0, final=True, payload=b"abcd", permit="permit") == 400,
         "short first final accepted")
    need(m.permit == "permit" and m.state == "idle",
         "malformed first request consumed permit")

    m = Model(expected)
    need(m.request(session="session", offset=0, final=False, payload=b"abcd", permit="bad") == 403,
         "wrong permit accepted")
    need(m.permit == "permit" and m.state == "idle",
         "wrong permit consumed authorization")

    m = Model(expected, now=30_000)
    need(m.request(session="session", offset=0, final=False, payload=b"abcd", permit="permit") == 409,
         "expired permit accepted")
    need(m.permit is None and m.state == "idle", "expired permit not invalidated")

    m = Model(expected)
    need(m.request(session="session", offset=0, final=False, payload=b"abcd", permit="permit") == 202,
         "valid first chunk rejected")
    need(m.permit is None and m.state == "writing" and m.seen == 4,
         "valid first chunk did not atomically consume permit")
    need(m.request(session="session", offset=4, final=True, payload=b"efgh", permit="permit") == 200,
         "valid final chunk rejected")
    need(m.state == "finalized" and m.seen == len(expected), "writer did not finalize")

    m = Model(expected)
    need(m.request(session="session", offset=0, final=False, payload=b"abcd", permit="permit") == 202,
         "setup failed")
    need(m.request(session="session", offset=3, final=True, payload=b"efgh") == 400,
         "wrong offset accepted")
    need(m.state == "failed" and m.failure == "offset_mismatch",
         "wrong offset not terminal")

    m = Model(expected)
    need(m.request(session="session", offset=0, final=False, payload=b"abcd", permit="permit") == 202,
         "setup failed")
    need(m.request(session="session", offset=4, final=False, payload=b"efgh") == 400,
         "exact-size non-final accepted")
    need(m.state == "failed" and m.failure == "invalid_chunk",
         "bad final semantics not terminal")

    expected_hash_source = b"abcdefgh"
    m = Model(expected_hash_source)
    need(m.request(session="session", offset=0, final=False, payload=b"abcd", permit="permit") == 202,
         "setup failed")
    # Same size, different data must fail final digest.
    need(m.request(session="session", offset=4, final=True, payload=b"EFGH") == 422,
         "wrong digest accepted")
    need(m.state == "failed" and m.failure == "sha256_mismatch",
         "wrong digest did not fail")

    print('OTA_WRITER_WEB_MODEL=PASS cases=8 permit_preserved_on_reject=1 replay_authority=blocked final_exact=1')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as exc:
        print(f'OTA_WRITER_WEB_MODEL=FAIL {exc}')
        raise SystemExit(1)
