#!/usr/bin/env python3
"""Static contract for the compile-only activation coordinator."""

from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components" / "usp_ota_guard"
HEADER = (COMP / "ota_activation_coordinator.h").read_text(encoding="utf-8")
SOURCE = (COMP / "ota_activation_coordinator.cpp").read_text(encoding="utf-8")
COMBINED = HEADER + SOURCE
GUARD = (
    (COMP / "ota_guard.h").read_text(encoding="utf-8")
    + (COMP / "ota_guard.cpp").read_text(encoding="utf-8")
    + (COMP / "__init__.py").read_text(encoding="utf-8")
)
BACKEND = (
    (COMP / "ota_boot_activation_backend.h").read_text(encoding="utf-8")
    + (COMP / "ota_boot_activation_backend.cpp").read_text(encoding="utf-8")
)
PROD = (ROOT / "firmware" / "ukraine-status-panel.yaml").read_text(
    encoding="utf-8"
)
PUBLIC = (
    ROOT / "firmware" / "ukraine-status-panel-public-first-flash.yaml"
).read_text(encoding="utf-8")


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    need(CURRENT_FIRMWARE_MARKER in PUBLIC, "wrong public version")

    for token in (
        "OtaActivationCoordinator",
        "ActivationCoordinatorInput",
        "ActivationCoordinatorSnapshot",
        "policy->evaluate",
        "ticket->consume",
        "backend->select",
        "ticket_consumed",
        "backend_attempted",
    ):
        need(token in COMBINED, f"missing coordinator token: {token}")

    policy_pos = SOURCE.find("policy->evaluate")
    consume_pos = SOURCE.find("ticket->consume")
    consumed_pos = SOURCE.find("ticket_consumed = true")
    backend_pos = SOURCE.find("backend->select")
    need(
        -1 not in (policy_pos, consume_pos, consumed_pos, backend_pos)
        and policy_pos < consume_pos < consumed_pos < backend_pos,
        "ordering must be policy -> consume -> consumed marker -> backend",
    )

    for forbidden in (
        "esp_ota_set_boot_partition",
        "esp_ota_begin",
        "esp_ota_write",
        "esp_ota_end",
        "esp_ota_abort",
        "esp_partition_write",
        "esp_partition_write_raw",
        "esp_partition_erase_range",
        "esp_restart",
        "esp_ota_mark_app_valid_cancel_rollback",
        "esp_ota_mark_app_invalid_rollback_and_reboot",
    ):
        need(
            forbidden not in COMBINED,
            f"coordinator contains direct mutation/reboot primitive: {forbidden}",
        )

    need(
        "bool runtime_enabled_{false};" in BACKEND,
        "boot backend is not hard-disabled",
    )
    for setter in ("set_runtime", "enable_runtime", "runtime_enabled_ = true"):
        need(setter not in BACKEND, f"boot backend exposes enable path: {setter}")

    for token in ("ota_activation_coordinator", "OtaActivationCoordinator"):
        need(token not in GUARD, f"coordinator wired into runtime guard: {token}")
        need(token not in PUBLIC, f"coordinator leaked into public profile: {token}")

    global_instance = re.search(
        r"(?m)^\s*(?:static\s+)?OtaActivationCoordinator\s+[A-Za-z_]\w*\s*[;{(]",
        SOURCE,
    )
    need(global_instance is None, "global/static coordinator instance exists")

    print(
        "OTA_ACTIVATION_COORDINATOR_STATIC=PASS "
        "ordering=policy-ticket-backend ticket_one_time=1 direct_mutation=0 "
        "backend_hard_disabled=1 unwired=1 public_isolated=1 no_reboot=1"
    )


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"OTA_ACTIVATION_COORDINATOR_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
