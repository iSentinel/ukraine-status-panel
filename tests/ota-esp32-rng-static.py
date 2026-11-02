#!/usr/bin/env python3
"""Guard the ESP32-safe libsodium entropy backend and init ordering."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "components/usp_ota_guard/ota_guard.cpp").read_text(encoding="utf-8")


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    required = (
        "#include <sodium/randombytes.h>",
        "ESP_IDF_RANDOMBYTES_IMPLEMENTATION",
        "esp_idf_randombytes_name",
        'return "esp-idf";',
        "esp_idf_randombytes_random",
        "return esp_random();",
        "esp_idf_randombytes_buf",
        "esp_fill_random(buf, size);",
        "randombytes_set_implementation(&ESP_IDF_RANDOMBYTES_IMPLEMENTATION)",
        "sodium_init()",
        "randombytes_implementation_name()",
    )
    for token in required:
        need(token in CPP, f"missing ESP32 randombytes contract: {token}")

    register = CPP.find(
        "randombytes_set_implementation(&ESP_IDF_RANDOMBYTES_IMPLEMENTATION)")
    sodium = CPP.find("sodium_init()", register)
    need(register >= 0 and sodium > register,
         "ESP-IDF randombytes backend must be selected before sodium_init")

    impl = CPP.find("static const randombytes_implementation ESP_IDF_RANDOMBYTES_IMPLEMENTATION")
    need(impl >= 0 and impl < register, "randombytes implementation lifetime/order invalid")
    need("randombytes_sysrandom" not in CPP,
         "OTA guard must not directly select libsodium sysrandom")
    print(
        "OTA_ESP32_RNG_STATIC=PASS backend=esp-idf register_before_sodium=1 "
        "esp_random=1 esp_fill_random=1 sysrandom_direct=0"
    )


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"OTA_ESP32_RNG_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
