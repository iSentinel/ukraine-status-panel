#!/usr/bin/env python3
"""Validate the isolated, compile-only ESP-IDF OTA backend foundation."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components/usp_ota_guard"
BACKEND_H = (COMP / "ota_flash_backend.h").read_text(encoding="utf-8")
BACKEND_CPP = (COMP / "ota_flash_backend.cpp").read_text(encoding="utf-8")
GUARD_H = (COMP / "ota_guard.h").read_text(encoding="utf-8")
GUARD_CPP = (COMP / "ota_guard.cpp").read_text(encoding="utf-8")
INIT = (COMP / "__init__.py").read_text(encoding="utf-8")
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD,
         "wrong production version")
    need(CURRENT_FIRMWARE_MARKER in PUBLIC,
         "wrong public version")

    for primitive in (
        "esp_ota_get_next_update_partition",
        "esp_ota_begin",
        "esp_ota_write",
        "esp_ota_end",
        "esp_ota_abort",
    ):
        need(primitive in BACKEND_CPP,
             f"compile-only backend primitive missing: {primitive}")

    for forbidden in (
        "esp_ota_set_boot_partition",
        "esp_restart",
        "App.safe_reboot",
        "esp_restart_noos",
    ):
        need(forbidden not in BACKEND_CPP + BACKEND_H + GUARD_CPP + GUARD_H,
             f"forbidden activation primitive present: {forbidden}")

    need('bool runtime_enabled_{false};' in BACKEND_H,
         "backend is not hard-disabled by default")
    need("runtime_enabled_ = true" not in BACKEND_CPP + BACKEND_H,
         "backend contains an enable assignment")
    need("set_runtime" not in (BACKEND_CPP + BACKEND_H).lower(),
         "backend exposes a runtime enable setter")
    need("enable_runtime" not in (BACKEND_CPP + BACKEND_H).lower(),
         "backend exposes a runtime enable method")

    begin_pos = BACKEND_CPP.find("bool OtaFlashBackend::begin")
    gate_pos = BACKEND_CPP.find("if (!this->runtime_enabled_)", begin_pos)
    partition_pos = BACKEND_CPP.find("esp_ota_get_next_update_partition", begin_pos)
    need(begin_pos >= 0 and gate_pos > begin_pos and partition_pos > gate_pos,
         "begin does not gate before selecting/writing OTA partition")

    for text, label in ((GUARD_CPP, "ota_guard.cpp"),
                        (GUARD_H, "ota_guard.h"),
                        (INIT, "__init__.py")):
        need("ota_flash_backend" not in text,
             f"backend header/source wired into {label}")
        need("OtaFlashBackend" not in text,
             f"backend class referenced by {label}")

    need("OtaFlashBackend" not in PROD and "ota_flash_backend" not in PROD,
         "backend manually instantiated from production YAML")
    need("usp_ota_guard" not in PUBLIC and "ota_flash_backend" not in PUBLIC,
         "private OTA backend leaked into public first-flash")

    for route_token in ("/usp/", "AsyncWeb", "handleRequest", "canHandle"):
        need(route_token not in BACKEND_CPP + BACKEND_H,
             f"backend unexpectedly exposes Web/runtime route surface: {route_token}")

    # The authenticated Web guard itself must still be flash-primitive-free.
    for primitive in ("esp_ota_begin", "esp_ota_write", "esp_ota_end",
                      "esp_ota_abort", "esp_ota_set_boot_partition"):
        need(primitive not in GUARD_CPP + GUARD_H,
             f"flash primitive leaked into authenticated Web guard: {primitive}")

    # No global/static backend object: object construction remains unreachable.
    global_instance = re.search(
        r"(?:^|\n)\s*(?:static\s+)?OtaFlashBackend\s+[A-Za-z_]\w*\s*[;{(]",
        BACKEND_CPP,
    )
    need(global_instance is None, "global/static backend instance exists")

    print(
        "OTA_FLASH_BACKEND_STATIC=PASS primitives=5 hard_disabled=1 "
        "unwired=1 no_boot_switch=1 no_reboot=1 public_isolated=1"
    )


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"OTA_FLASH_BACKEND_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
