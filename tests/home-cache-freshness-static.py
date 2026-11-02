#!/usr/bin/env python3
"""Validate Home resident-frame data/minute freshness wiring."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
YAML = (ROOT / "firmware" / "ukraine-status-panel.yaml").read_text()
HEADER = (ROOT / "components" / "lvgl" / "lvgl_esphome.h").read_text()
CPP = (ROOT / "components" / "lvgl" / "lvgl_esphome.cpp").read_text()


def require(text: str, needle: str, message: str) -> None:
    """Require one exact freshness contract fragment."""
    if needle not in text:
        raise AssertionError(message)


def check_cpp_contract() -> None:
    """Require data-key freshness without poisoning same-key fast cache."""
    require(
        HEADER,
        "set_home_native_frame_data_key(uint64_t key)",
        "Home data-key API missing",
    )
    require(
        CPP,
        "home_native_frame_cache_data_key_ !=",
        "present path lacks data-key guard",
    )
    if "home_native_frame_cache_render_generation_" in CPP:
        raise AssertionError("render-generation over-invalidation returned")


def check_yaml_key() -> None:
    """Require generation and trusted-minute keys for capture/present."""
    require(
        YAML,
        "id: home_native_frame_data_generation",
        "Home data generation global missing",
    )
    require(
        YAML,
        "id(home_native_frame_data_generation)) << 32U",
        "Home cache key lacks data generation",
    )
    require(
        YAML,
        "static_cast<uint32_t>(now.minute)",
        "Home cache key lacks current minute",
    )
    calls = YAML.count("set_home_native_frame_data_key(data_key)")
    if calls != 2:
        raise AssertionError(
            f"expected capture+present data-key calls, got {calls}"
        )


def check_source_generation() -> None:
    """Require Home-visible asynchronous sources to age the cache."""
    generation = "id(home_native_frame_data_generation)++;"
    count = YAML.count(generation)
    if count < 6:
        raise AssertionError(
            f"Home data generation coverage too small: {count}"
        )
    require(
        YAML,
        "id(home_native_frame_data_generation)++;\n              return true;",
        "alert signature change does not age Home cache",
    )
    wifi = YAML[YAML.index("\nwifi:\n") : YAML.index("\ncaptive_portal:")]
    disconnect = wifi[wifi.index("on_disconnect:") :]
    require(
        disconnect,
        generation,
        "Wi-Fi disconnect does not age Home cache",
    )
    require(
        disconnect,
        "id(panel_wifi_rssi_valid) = false;",
        "Wi-Fi disconnect does not invalidate cached RSSI",
    )
    require(
        YAML,
        "- script.execute: refresh_dashboard_ui\n"
        "          - script.execute: reveal_dashboard_home_startup",
        "Home entry does not refresh current dashboard data",
    )


def main() -> None:
    """Run the static C-D1 freshness contract."""
    check_cpp_contract()
    check_yaml_key()
    check_source_generation()
    print("HOME_CACHE_FRESHNESS_STATIC=PASS data_generation minute sources same_key")


if __name__ == "__main__":
    main()
