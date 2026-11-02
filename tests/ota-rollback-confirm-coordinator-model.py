#!/usr/bin/env python3
"""Model fail-closed rollback confirmation coordination."""

from dataclasses import dataclass


@dataclass
class Backend:
    result: str = "disabled"
    calls: int = 0

    def confirm(self) -> str:
        self.calls += 1
        return self.result


def execute(policy: str, backend: Backend, args_ok: bool = True) -> str:
    if not args_ok:
        return "invalid_argument"
    if policy == "already_valid":
        return "already_valid"
    if policy != "eligible":
        return "policy_blocked"
    result = backend.confirm()
    if result in ("confirmed", "already_valid"):
        return result
    return "backend_failed"


def main() -> None:
    backend = Backend("confirmed")
    assert execute("blocked", backend) == "policy_blocked"
    assert backend.calls == 0

    backend = Backend("confirmed")
    assert execute("eligible", backend, args_ok=False) == "invalid_argument"
    assert backend.calls == 0

    backend = Backend("confirmed")
    assert execute("already_valid", backend) == "already_valid"
    assert backend.calls == 0

    backend = Backend("disabled")
    assert execute("eligible", backend) == "backend_failed"
    assert backend.calls == 1

    backend = Backend("failed")
    assert execute("eligible", backend) == "backend_failed"
    assert backend.calls == 1

    backend = Backend("confirmed")
    assert execute("eligible", backend) == "confirmed"
    assert backend.calls == 1

    backend = Backend("already_valid")
    assert execute("eligible", backend) == "already_valid"
    assert backend.calls == 1

    backend = Backend("unexpected")
    assert execute("eligible", backend) == "backend_failed"
    assert backend.calls == 1

    print(
        "OTA_ROLLBACK_CONFIRM_COORDINATOR_MODEL=PASS cases=8 "
        "blocked_before_backend=1 already_valid_bypass=1 eligible_only=1 "
        "disabled_fail_closed=1 race_already_valid=1"
    )


if __name__ == "__main__":
    main()
