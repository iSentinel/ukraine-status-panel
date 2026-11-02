#!/usr/bin/env python3
"""Model the OTA preflight fixed-window throttles, including uint32 wrap."""
from dataclasses import dataclass

MASK32 = 0xFFFFFFFF


@dataclass
class Window:
    started_ms: int = 0
    count: int = 0
    initialized: bool = False


def elapsed_u32(now: int, start: int) -> int:
    return (now - start) & MASK32


def allow(window: Window, limit: int, period_ms: int, now: int) -> tuple[bool, int]:
    now &= MASK32
    if not window.initialized or elapsed_u32(now, window.started_ms) >= period_ms:
        window.started_ms = now
        window.count = 0
        window.initialized = True
    elapsed = elapsed_u32(now, window.started_ms)
    if window.count >= limit:
        return False, 0 if elapsed >= period_ms else period_ms - elapsed
    window.count += 1
    return True, 0


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def test_boundary() -> None:
    window = Window()
    for _ in range(5):
        need(allow(window, 5, 60_000, 1_000) == (True, 0), "allowed request rejected")
    need(allow(window, 5, 60_000, 1_000) == (False, 60_000), "limit boundary wrong")
    need(allow(window, 5, 60_000, 60_999) == (False, 1), "retry boundary wrong")
    need(allow(window, 5, 60_000, 61_000) == (True, 0), "window did not reset")
    need(window.count == 1, "new window count wrong")


def test_wrap() -> None:
    start = 0xFFFFFF00
    window = Window(started_ms=start, count=1, initialized=True)
    now = (start + 500) & MASK32
    need(elapsed_u32(now, start) == 500, "uint32 elapsed is not wrap-safe")
    need(allow(window, 2, 1_000, now) == (True, 0), "wrapped in-window request rejected")
    need(allow(window, 2, 1_000, now) == (False, 500), "wrapped retry wrong")
    reset_now = (start + 1_000) & MASK32
    need(allow(window, 2, 1_000, reset_now) == (True, 0), "wrapped window did not reset")


def test_independent_windows() -> None:
    manifest = Window()
    reset = Window()
    bad_session = Window()
    for _ in range(5):
        need(allow(manifest, 5, 60_000, 42)[0], "manifest allowance failed")
        need(allow(reset, 5, 30_000, 42)[0], "reset allowance failed")
    for _ in range(10):
        need(allow(bad_session, 10, 30_000, 42)[0], "bad-session allowance failed")
    need(not allow(manifest, 5, 60_000, 42)[0], "manifest limiter missing")
    need(not allow(reset, 5, 30_000, 42)[0], "reset limiter missing")
    need(not allow(bad_session, 10, 30_000, 42)[0], "bad-session limiter missing")
    need(manifest.count == 5 and reset.count == 5 and bad_session.count == 10,
         "rejected requests changed counters")


def main() -> None:
    test_boundary()
    test_wrap()
    test_independent_windows()
    print("OTA_RATE_LIMIT_MODEL=PASS boundary=1 wrap=1 independent=3")


if __name__ == "__main__":
    main()
