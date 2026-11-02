#!/usr/bin/env python3
"""Guard HTTPS gate telemetry, priority, and bounded admission contracts."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
GATE = (ROOT / "components/usp_weather/network_gate.h").read_text()
CLIENTS = {
    "weather": (
        ROOT / "components/usp_weather/weather_client.cpp"
    ).read_text(),
    "outage": (ROOT / "components/usp_outage/outage_client.cpp").read_text(),
    "svitlobot": (
        ROOT / "components/usp_svitlobot/svitlobot_client.cpp"
    ).read_text(),
    "neptun": (ROOT / "components/usp_neptun/neptun_client.cpp").read_text(),
}


def need(ok: bool, message: str) -> None:
    """Fail one static contract with a readable message."""
    if not ok:
        raise AssertionError(message)


def check_gate() -> None:
    """Require telemetry plus bounded priority admission in the shared gate."""
    for token in (
        "class HttpsTransaction final",
        "enum class HttpsPriority",
        "BACKGROUND",
        "NORMAL",
        "CRITICAL",
        "HTTPS_GATE_WAIT",
        "HTTPS_GATE_TIMEOUT",
        "HTTPS_GATE_ACQUIRE",
        "HTTPS_GATE_RELEASE",
        "https_active_owner",
        "https_active_since_ms",
        "https_waiters",
        "https_critical_waiters",
        "std::unique_lock<std::mutex>",
        "try_lock()",
        "bool acquired() const",
        "wait_timeout_ms_",
    ):
        need(token in GATE, f"HTTPS gate contract missing: {token}")


def check_clients() -> None:
    """Require explicit priority, bounded waits, and fail-fast checks."""
    need("HttpsPriority::NORMAL, 2500" in CLIENTS["weather"],
         "weather priority/timeout missing")
    need("HttpsPriority::NORMAL, 2500" in CLIENTS["outage"],
         "outage priority/timeout missing")
    need("HttpsPriority::BACKGROUND, 500" in CLIENTS["svitlobot"],
         "SvitloBot background timeout missing")
    neptun = CLIENTS["neptun"]
    need(neptun.count("HttpsPriority::CRITICAL, 500") == 2,
         "NEPTUN threats/alerts must both be CRITICAL 500ms")
    need("GATE_BUSY_RETRY_MS = 2000" in (
        ROOT / "components/usp_neptun/neptun_client.h"
    ).read_text(), "NEPTUN gate-busy short retry constant missing")
    need(neptun.count("https_gate_busy_during_request_ = true") == 2,
         "NEPTUN must classify both gate-admission failures")
    need("schedule_gate_retry_()" in neptun,
         "NEPTUN gate contention must avoid provider backoff growth")
    for name, text in CLIENTS.items():
        need("network_tx.acquired()" in text,
             f"{name} does not handle gate timeout")
        need(
            "lock_guard<std::mutex> network_lock(usp_network::https_mutex())"
            not in text,
            f"legacy opaque HTTPS lock remains in {name}",
        )


def main() -> int:
    """Validate the source-level HTTPS gate contract."""
    check_gate()
    check_clients()
    print(
        "NETWORK_HTTPS_GATE_STATIC=PASS serialized=1 priority=1 "
        "bounded_wait=1 fail_fast=1 clients=4"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"NETWORK_HTTPS_GATE_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
