#!/usr/bin/env python3
"""Model the fail-closed read-only rollback health decision."""
from dataclasses import dataclass

MIN_STABLE_UPTIME_MS = 30000


@dataclass
class Evidence:
    startup_complete: bool = True
    core_services_ready: bool = True
    config_ready: bool = True
    display_ready: bool = True
    fatal_fault_free: bool = True
    stable_uptime_ms: int = MIN_STABLE_UPTIME_MS


def evaluate(*, running=True, app=True, boot_matches=True, query_ok=True,
             state="pending", evidence=Evidence()):
    if not running:
        return "blocked", "invalid_running_partition"
    if not app:
        return "blocked", "running_not_app"
    if not boot_matches:
        return "blocked", "boot_partition_mismatch"
    if not query_ok:
        return "blocked", "state_query_failed"
    if state == "valid":
        return "already_valid", "none"
    if state != "pending":
        return "blocked", "state_not_pending_verify"
    if not evidence.startup_complete:
        return "blocked", "startup_incomplete"
    if not evidence.core_services_ready:
        return "blocked", "core_services_not_ready"
    if not evidence.config_ready:
        return "blocked", "config_not_ready"
    if not evidence.display_ready:
        return "blocked", "display_not_ready"
    if not evidence.fatal_fault_free:
        return "blocked", "fatal_fault_present"
    if evidence.stable_uptime_ms < MIN_STABLE_UPTIME_MS:
        return "blocked", "stable_uptime_too_short"
    return "eligible", "none"


def main():
    assert evaluate(running=False) == ("blocked", "invalid_running_partition")
    assert evaluate(app=False) == ("blocked", "running_not_app")
    assert evaluate(boot_matches=False) == ("blocked", "boot_partition_mismatch")
    assert evaluate(query_ok=False) == ("blocked", "state_query_failed")
    assert evaluate(state="valid") == ("already_valid", "none")
    for state in ("new", "invalid", "aborted", "undefined"):
        assert evaluate(state=state) == ("blocked", "state_not_pending_verify")

    cases = [
        (Evidence(startup_complete=False), "startup_incomplete"),
        (Evidence(core_services_ready=False), "core_services_not_ready"),
        (Evidence(config_ready=False), "config_not_ready"),
        (Evidence(display_ready=False), "display_not_ready"),
        (Evidence(fatal_fault_free=False), "fatal_fault_present"),
        (Evidence(stable_uptime_ms=MIN_STABLE_UPTIME_MS - 1),
         "stable_uptime_too_short"),
    ]
    for evidence, reason in cases:
        assert evaluate(evidence=evidence) == ("blocked", reason)

    assert evaluate(evidence=Evidence(stable_uptime_ms=MIN_STABLE_UPTIME_MS)) == (
        "eligible", "none"
    )
    assert evaluate(evidence=Evidence(stable_uptime_ms=MIN_STABLE_UPTIME_MS + 1)) == (
        "eligible", "none"
    )

    print(
        "OTA_ROLLBACK_HEALTH_POLICY_MODEL=PASS cases=17 "
        "pending_only=1 already_valid=1 local_health=5 uptime_boundary=1 "
        "network_independent=1 fail_closed=1"
    )


if __name__ == "__main__":
    main()
