#!/usr/bin/env python3
# Static guard for crash-safe large JSON serialization on ESP32-S3.

from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "components/json/json_util.h"
CPP = ROOT / "components/json/json_util.cpp"
YAML = ROOT / "firmware/ukraine-status-panel.yaml"


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"{label}: missing {needle!r}")


def main() -> None:
    header = HEADER.read_text()
    cpp = CPP.read_text()
    yaml = YAML.read_text()

    if "new char[" in header or "delete[]" in header:
        raise AssertionError(
            "SerializationBuffer still uses abort-prone new[]/delete[]"
        )
    require(header, "RAMAllocator<char>", "PSRAM-first serialization allocator")
    require(
        header,
        "bool reallocate_heap_(size_t size)",
        "checked reallocation",
    )
    require(header, "candidate == nullptr", "allocation failure check")
    require(header, "reset_to_fallback_json_", "valid JSON fallback")
    require(header, "stack_buffer_[0] = '{';", "fallback opening brace")
    require(header, "stack_buffer_[1] = '}';", "fallback closing brace")
    require(
        cpp,
        "if (!result.reallocate_heap_(heap_size - 1))",
        "caller failure handling",
    )
    require(
        cpp,
        'ESP_LOGE(TAG, "Could not allocate %zu bytes for JSON serialization"',
        "OOM logging",
    )
    require(
        cpp,
        'ESP_LOGE(TAG, "JSON payload exceeds %zu-byte serialization limit"',
        "oversize logging",
    )
    require(cpp, "result.reset_to_fallback_json_();", "fail-closed JSON")
    require(
        yaml,
        CURRENT_FIRMWARE_MARKER,
        "candidate version",
    )
    external = yaml.split("external_components:", 1)[1].split("\n\n", 1)[0]
    require(external, "      - json", "local json external component")
    print(
        "PASS: large JSON serialization is PSRAM-first and fails closed "
        "without new[] abort"
    )


if __name__ == "__main__":
    main()
