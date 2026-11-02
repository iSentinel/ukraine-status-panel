#!/usr/bin/env python3
"""Fail closed on WX-6 integration and pending-refresh semantics."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "components/usp_weather/weather_client.cpp").read_text()
HEADER = (ROOT / "components/usp_weather/weather_client.h").read_text()
GATE = (ROOT / "include/usp_weather_refresh_gate.h").read_text()
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()


def main() -> None:
    assert "MIN_ATTEMPT_INTERVAL_MS = 60'000U" in GATE
    assert "uint32_t last_attempt_ms{0};" in GATE
    assert "bool attempted{false};" in GATE
    assert "now - then" in GATE
    assert "forecast_attempt_state_" in HEADER
    assert "refresh_requested_.load(std::memory_order_acquire)" in CPP
    assert "usp_weather_refresh::should_start(" in CPP
    assert "usp_weather_refresh::mark_started(" in CPP
    assert "refresh_requested_.exchange(\n            false, std::memory_order_acq_rel)" in CPP
    # Old consume-before-gate bypass must stay gone.
    assert "const bool manual = this->refresh_requested_.exchange(false);" not in CPP
    worker = CPP[CPP.index("void WeatherClient::worker_loop()") : CPP.index("WeatherClient::SearchRequest", CPP.index("void WeatherClient::worker_loop()"))]
    assert worker.index("search_requested_.load(std::memory_order_acquire)") < worker.index("search_retry_gate_.ready(now)") < worker.index("search_requested_.exchange(false, std::memory_order_acq_rel)")
    # Existing automatic cadence/backoff remains intact.
    assert "this->next_refresh_ms_ = millis() + this->refresh_interval_ms_;" in CPP
    assert "std::min(this->retry_delay_ms_ * 2U, MAX_RETRY_MS)" in CPP
    assert "refresh_interval: 15min" in YAML and "stale_after: 45min" in YAML
    assert YAML.count("request_refresh();") >= 4
    print("WEATHER_REFRESH_GATE_STATIC=PASS project_gap60s pending_preserved search_independent auto15m retry_backoff wrap_helper callsites>=4")


if __name__ == "__main__":
    main()
