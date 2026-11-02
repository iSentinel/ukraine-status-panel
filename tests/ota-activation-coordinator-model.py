#!/usr/bin/env python3
"""Model the fail-closed activation coordinator ordering."""

from dataclasses import dataclass


@dataclass
class Ticket:
    active: bool = True
    token: str = "good"
    consumed: bool = False
    failure: str = "none"

    def consume(self, supplied: str) -> bool:
        if not self.active:
            self.failure = "not_active"
            return False
        if supplied != self.token:
            self.failure = "token_mismatch"
            return False
        self.active = False
        self.consumed = True
        self.failure = "none"
        return True


@dataclass
class Backend:
    enabled: bool = False
    succeed: bool = True
    calls: int = 0

    def select(self) -> bool:
        self.calls += 1
        return self.enabled and self.succeed


def execute(policy_ok: bool, supplied: str, ticket: Ticket, backend: Backend,
            args_ok: bool = True) -> str:
    if not args_ok:
        return "invalid_argument"
    if not policy_ok:
        return "policy_blocked"
    if not ticket.consume(supplied):
        return "ticket_rejected"
    if not backend.select():
        return "backend_failed"
    return "activated"


def main() -> None:
    ticket = Ticket()
    backend = Backend()
    assert execute(False, "good", ticket, backend) == "policy_blocked"
    assert ticket.active and not ticket.consumed and backend.calls == 0

    ticket = Ticket()
    backend = Backend()
    assert execute(True, "good", ticket, backend, args_ok=False) == "invalid_argument"
    assert ticket.active and not ticket.consumed and backend.calls == 0

    ticket = Ticket()
    backend = Backend()
    assert execute(True, "wrong", ticket, backend) == "ticket_rejected"
    assert ticket.active and not ticket.consumed and backend.calls == 0

    ticket = Ticket()
    backend = Backend(enabled=False)
    assert execute(True, "good", ticket, backend) == "backend_failed"
    assert ticket.consumed and not ticket.active and backend.calls == 1
    assert execute(True, "good", ticket, backend) == "ticket_rejected"
    assert backend.calls == 1

    ticket = Ticket()
    backend = Backend(enabled=True, succeed=True)
    assert execute(True, "good", ticket, backend) == "activated"
    assert ticket.consumed and backend.calls == 1
    assert execute(True, "good", ticket, backend) == "ticket_rejected"
    assert backend.calls == 1

    ticket = Ticket()
    backend = Backend(enabled=True, succeed=False)
    assert execute(True, "good", ticket, backend) == "backend_failed"
    assert ticket.consumed and backend.calls == 1
    assert execute(True, "good", ticket, backend) == "ticket_rejected"
    assert backend.calls == 1

    first = Ticket()
    backend = Backend(enabled=True, succeed=False)
    assert execute(True, "good", first, backend) == "backend_failed"
    fresh = Ticket(token="fresh")
    backend.succeed = True
    assert execute(True, "fresh", fresh, backend) == "activated"
    assert fresh.consumed and backend.calls == 2

    print(
        "OTA_ACTIVATION_COORDINATOR_MODEL=PASS cases=7 "
        "policy_preserves_ticket=1 reject_before_backend=1 "
        "backend_failure_consumes_ticket=1 replay=blocked fresh_retry=1"
    )


if __name__ == "__main__":
    main()
