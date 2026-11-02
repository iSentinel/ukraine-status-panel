#!/usr/bin/env python3
"""Model the hard-disabled rollback-confirm backend ordering."""


class Model:
    def __init__(self, enabled=False):
        self.enabled = enabled
        self.running = True
        self.running_app = True
        self.boot_matches = True
        self.query_ok = True
        self.state = "pending"
        self.mark_ok = True
        self.readback_ok = True
        self.mark_calls = 0

    def confirm(self):
        if not self.enabled:
            return "disabled"
        if not self.running:
            return "invalid_running"
        if not self.running_app:
            return "running_not_app"
        if not self.boot_matches:
            return "boot_mismatch"
        if not self.query_ok:
            return "state_query_failed"
        if self.state == "valid":
            return "already_valid"
        if self.state != "pending":
            return "state_not_pending"
        self.mark_calls += 1
        if not self.mark_ok:
            return "mark_failed"
        if not self.readback_ok:
            return "verify_failed"
        self.state = "valid"
        return "confirmed"


def main():
    m = Model(False)
    assert m.confirm() == "disabled" and m.mark_calls == 0

    m = Model(True)
    m.running = False
    assert m.confirm() == "invalid_running" and m.mark_calls == 0

    m = Model(True)
    m.running_app = False
    assert m.confirm() == "running_not_app" and m.mark_calls == 0

    m = Model(True)
    m.boot_matches = False
    assert m.confirm() == "boot_mismatch" and m.mark_calls == 0

    m = Model(True)
    m.query_ok = False
    assert m.confirm() == "state_query_failed" and m.mark_calls == 0

    m = Model(True)
    m.state = "valid"
    assert m.confirm() == "already_valid" and m.mark_calls == 0

    for state in ("new", "invalid", "aborted", "undefined"):
        m = Model(True)
        m.state = state
        assert m.confirm() == "state_not_pending" and m.mark_calls == 0

    m = Model(True)
    m.mark_ok = False
    assert m.confirm() == "mark_failed" and m.mark_calls == 1

    m = Model(True)
    m.readback_ok = False
    assert m.confirm() == "verify_failed" and m.mark_calls == 1

    m = Model(True)
    assert m.confirm() == "confirmed" and m.mark_calls == 1
    assert m.confirm() == "already_valid" and m.mark_calls == 1

    print(
        "OTA_ROLLBACK_CONFIRM_BACKEND_MODEL=PASS cases=14 gate_before_reads=1 "
        "already_valid_no_mark=1 pending_only=1 mark_once=1 readback=1 "
        "idempotent_retry=1 fail_closed=1"
    )


if __name__ == "__main__":
    main()
