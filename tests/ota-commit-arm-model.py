#!/usr/bin/env python3
"""Model commit-arm lifecycle, replay resistance, expiry and uint32 wrap."""
from dataclasses import dataclass

MASK32 = 0xFFFFFFFF
TTL = 30_000


@dataclass
class Arm:
    token: str = ''
    active: bool = False
    started_ms: int = 0

    def create(self, token: str, now: int, verified: bool, session: bool) -> bool:
        if not verified or not session:
            return False
        self.clear()
        self.token = token
        self.active = True
        self.started_ms = now & MASK32
        return True

    def expired(self, now: int) -> bool:
        return self.active and (((now & MASK32) - self.started_ms) & MASK32) >= TTL

    def expire(self, now: int) -> None:
        if self.expired(now):
            self.clear()

    def consume(self, supplied: str, now: int, verified: bool, session: bool) -> bool:
        self.expire(now)
        if not verified or not session or not self.active or supplied != self.token:
            return False
        self.clear()
        return True

    def clear(self) -> None:
        self.token = ''
        self.active = False
        self.started_ms = 0


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    arm = Arm()
    need(not arm.create('a', 100, verified=False, session=True), 'armed before verify')
    need(not arm.create('a', 100, verified=True, session=False), 'armed without session')
    need(arm.create('a', 100, verified=True, session=True), 'valid arm failed')
    need(not arm.consume('bad', 101, True, True), 'wrong challenge consumed')
    need(arm.active, 'wrong challenge mutated arm')
    need(arm.consume('a', 102, True, True), 'valid challenge failed')
    need(not arm.consume('a', 103, True, True), 'replay succeeded')

    need(arm.create('b', 1_000, True, True), 'expiry setup failed')
    need(not arm.expired(30_999), 'expired too early')
    need(arm.expired(31_000), 'did not expire at TTL')
    need(not arm.consume('b', 31_000, True, True), 'expired challenge consumed')

    start = 0xFFFFFF00
    need(arm.create('c', start, True, True), 'wrap setup failed')
    before = (start + TTL - 1) & MASK32
    at = (start + TTL) & MASK32
    need(not arm.expired(before), 'wrap challenge expired early')
    need(arm.expired(at), 'wrap challenge failed to expire')

    need(arm.create('d', 77, True, True), 'session clear setup failed')
    arm.clear()
    need(not arm.consume('d', 78, True, True), 'cleared challenge remained usable')
    print('OTA_COMMIT_ARM_MODEL=PASS transitions=12 replay=blocked wrap=1')


if __name__ == '__main__':
    main()
