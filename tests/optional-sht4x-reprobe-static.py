#!/usr/bin/env python3
"""Fail closed on bounded SHT4x discovery/reconnect probe cadence."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "components/usp_optional_sht4x/optional_sht4x.cpp").read_text()
HEADER = (ROOT / "components/usp_optional_sht4x/optional_sht4x.h").read_text()
POLICY_PATH = ROOT / "components/usp_optional_sht4x/reprobe_policy.h"
SENSOR = (ROOT / "components/usp_optional_sht4x/sensor.py").read_text()


def main() -> None:
    assert POLICY_PATH.exists(), "missing reprobe_policy.h"
    policy = POLICY_PATH.read_text()
    assert "NEVER_SEEN_INTERVAL_MS = 300000U" in policy
    assert "RECONNECT_INTERVAL_MS = 60000U" in policy
    assert "now - this->due_ms_" in policy
    assert "0x80000000U" in policy
    assert "ReprobePolicy reprobe_policy_" in HEADER
    assert "probe_if_due_" in HEADER
    assert "reprobe_policy_.schedule(now, this->ever_detected_)" in CPP
    assert "reprobe_policy_.schedule(millis(), true)" in CPP
    assert "if (!this->detected_ && !this->probe_())" not in CPP
    assert 'polling_component_schema("30s")' in SENSOR
    print("OPTIONAL_SHT4X_REPROBE_STATIC=PASS boot=immediate absent=300s reconnect=60s measurement=30s wrap=1")


if __name__ == "__main__":
    main()
