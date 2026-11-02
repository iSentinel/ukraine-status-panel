#!/usr/bin/env python3
"""Model quiet absence, disconnect fault persistence and SHT4x hot-plug."""

from dataclasses import dataclass


@dataclass
class ShtState:
    """Minimal state machine matching OptionalSHT4XComponent fault policy."""

    detected: bool = False
    ever_detected: bool = False
    failures: int = 0
    warning: str = ""

    def probe(self, result: str) -> None:
        """Apply one presence probe result without producing repeated absence faults."""
        if result == "nack":
            if not self.ever_detected:
                self.warning = ""
            return
        if result != "ok":
            self.warning = "probe fault"
            return
        self.detected = True
        self.ever_detected = True
        self.failures = 0
        self.warning = ""

    def io_result(self, ok: bool) -> None:
        """Apply one measurement result and latch a disconnect after three failures."""
        if ok:
            self.failures = 0
            self.warning = ""
            return
        self.failures += 1
        if self.failures < 3:
            self.warning = "retry"
            return
        self.detected = False
        self.failures = 0
        self.warning = "disconnected"


def require(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    state = ShtState()
    state.probe("nack")
    require(not state.detected and not state.warning, "initial absence must be quiet")
    state.probe("bus")
    require(state.warning == "probe fault", "real probe bus fault must be visible")
    state.probe("nack")
    require(not state.warning, "never-seen NACK must clear transient probe fault")
    state.probe("ok")
    require(state.detected and state.ever_detected and not state.warning, "hot-plug detect failed")
    state.io_result(False)
    state.io_result(False)
    require(state.detected and state.warning == "retry", "measurement retry policy broken")
    state.io_result(False)
    require(not state.detected and state.warning == "disconnected", "disconnect must latch warning")
    state.probe("nack")
    require(state.warning == "disconnected", "absent re-probe must preserve disconnect warning")
    state.probe("ok")
    require(state.detected and not state.warning, "reconnect must clear fault")
    state.io_result(True)
    require(not state.warning and state.failures == 0, "healthy measurement must stay clean")
    print("OPTIONAL_SHT4X_STATE_MODEL=PASS cases=9 quiet_absence=1 disconnect_latched=1 hotplug=1")


if __name__ == "__main__":
    main()
