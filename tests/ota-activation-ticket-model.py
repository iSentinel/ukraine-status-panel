#!/usr/bin/env python3
"""Behavior model for one-time OTA activation ticket semantics."""
from dataclasses import dataclass

TTL = 30_000
MASK32 = 0xFFFF_FFFF


@dataclass
class Ticket:
    token: str = "a" * 32
    session: bytes = b"S" * 16
    target: int = 0x410000
    digest: bytes = b"D" * 32
    version: str = "0.115.235-dev"
    started: int = 1000
    state: str = "active"
    failure: str = "none"

    def elapsed(self, now: int) -> int:
        return (now - self.started) & MASK32

    def expire(self, now: int) -> None:
        if self.state == "active" and self.elapsed(now) >= TTL:
            self.state = "expired"
            self.failure = "expired"

    def consume(self, token: str, session: bytes, target: int,
                digest: bytes, version: str, now: int) -> bool:
        self.expire(now)
        if self.state != "active":
            if self.state != "expired":
                self.failure = "not_active"
            return False
        checks = [
            (token == self.token, "token_mismatch"),
            (session == self.session, "session_mismatch"),
            (target == self.target, "target_mismatch"),
            (digest == self.digest, "digest_mismatch"),
            (version == self.version, "version_mismatch"),
        ]
        for ok, failure in checks:
            if not ok:
                self.failure = failure
                return False
        self.state = "consumed"
        self.failure = "none"
        self.token = ""
        self.session = bytes(16)
        self.digest = bytes(32)
        self.version = ""
        self.target = 0
        return True


def issue(eligible: bool, valid_args: bool = True, started: int = 1000) -> Ticket | None:
    if not eligible or not valid_args:
        return None
    return Ticket(started=started)


def main() -> None:
    assert issue(False) is None
    assert issue(True, False) is None

    for field, bad_value, expected in [
        ("token", "b" * 32, "token_mismatch"),
        ("session", b"X" * 16, "session_mismatch"),
        ("target", 0x810000, "target_mismatch"),
        ("digest", b"X" * 32, "digest_mismatch"),
        ("version", "0.0.0", "version_mismatch"),
    ]:
        ticket = Ticket()
        args = dict(token=ticket.token, session=ticket.session, target=ticket.target,
                    digest=ticket.digest, version=ticket.version, now=1001)
        args[field] = bad_value
        assert not ticket.consume(**args)
        assert ticket.state == "active", (field, ticket.state)
        assert ticket.failure == expected, (field, ticket.failure)
        assert ticket.consume("a" * 32, b"S" * 16, 0x410000,
                              b"D" * 32, "0.115.235-dev", 1002)
        assert ticket.state == "consumed"

    replay = Ticket()
    assert replay.consume(replay.token, replay.session, replay.target,
                          replay.digest, replay.version, 1001)
    assert not replay.consume("a" * 32, b"S" * 16, 0x410000,
                              b"D" * 32, "0.115.235-dev", 1002)
    assert replay.failure == "not_active"

    expired = Ticket(started=1000)
    assert not expired.consume(expired.token, expired.session, expired.target,
                               expired.digest, expired.version, 31_000)
    assert expired.state == "expired"

    boundary = Ticket(started=1000)
    assert boundary.consume(boundary.token, boundary.session, boundary.target,
                            boundary.digest, boundary.version, 30_999)

    wrap = Ticket(started=0xFFFF_F000)
    before = (wrap.started + TTL - 1) & MASK32
    assert wrap.consume(wrap.token, wrap.session, wrap.target,
                        wrap.digest, wrap.version, before)
    wrap_expired = Ticket(started=0xFFFF_F000)
    at_ttl = (wrap_expired.started + TTL) & MASK32
    assert not wrap_expired.consume(wrap_expired.token, wrap_expired.session,
                                    wrap_expired.target, wrap_expired.digest,
                                    wrap_expired.version, at_ttl)
    assert wrap_expired.state == "expired"

    print("OTA_ACTIVATION_TICKET_MODEL=PASS cases=12 replay=blocked wrong_binding_preserves=1 ttl_boundary=1 wrap_safe=1")


if __name__ == "__main__":
    main()
