#!/usr/bin/env python3
"""Model the fail-closed ordering of the future boot-selection backend."""
from dataclasses import dataclass


@dataclass
class Partition:
    address: int
    app: bool = True
    canonical: bool = True


class Model:
    def __init__(self, enabled=False, running=0x10000):
        self.enabled = enabled
        self.running = running
        self.boot = running
        self.set_calls = 0
        self.set_ok = True
        self.readback_override = None
        self.state = "disabled"
        self.failure = "none"

    def select(self, target):
        self.state = "disabled" if not self.enabled else "idle"
        self.failure = "none"
        if not self.enabled:
            self.failure = "disabled"
            return False
        if target is None or not target.canonical:
            self.state, self.failure = "failed", "invalid_target"
            return False
        if not target.app:
            self.state, self.failure = "failed", "not_app_partition"
            return False
        if target.address == self.running:
            self.state, self.failure = "failed", "target_is_running"
            return False
        self.state = "activating"
        self.set_calls += 1
        if not self.set_ok:
            self.state, self.failure = "failed", "set_boot_failed"
            return False
        self.boot = target.address
        observed = self.boot if self.readback_override is None else self.readback_override
        if observed != target.address:
            self.state, self.failure = "failed", "verify_boot_failed"
            return False
        self.state, self.failure = "activated", "none"
        return True


def main():
    target = Partition(0x810000)

    m = Model(enabled=False)
    assert not m.select(target) and m.set_calls == 0 and m.failure == "disabled"

    m = Model(enabled=True)
    assert not m.select(None) and m.set_calls == 0 and m.failure == "invalid_target"

    m = Model(enabled=True)
    assert not m.select(Partition(0x810000, canonical=False))
    assert m.set_calls == 0 and m.failure == "invalid_target"

    m = Model(enabled=True)
    assert not m.select(Partition(0x810000, app=False))
    assert m.set_calls == 0 and m.failure == "not_app_partition"

    m = Model(enabled=True, running=0x810000)
    assert not m.select(target) and m.set_calls == 0 and m.failure == "target_is_running"

    m = Model(enabled=True)
    m.set_ok = False
    assert not m.select(target) and m.set_calls == 1 and m.failure == "set_boot_failed"

    m = Model(enabled=True)
    m.readback_override = 0x10000
    assert not m.select(target) and m.set_calls == 1 and m.failure == "verify_boot_failed"

    m = Model(enabled=True)
    assert m.select(target) and m.set_calls == 1 and m.state == "activated"
    assert m.boot == target.address and m.failure == "none"

    print(
        "OTA_BOOT_ACTIVATION_BACKEND_MODEL=PASS cases=8 gate_before_set=1 "
        "canonical_app_guard=1 running_guard=1 readback=1 fail_closed=1"
    )


if __name__ == "__main__":
    main()
