#!/usr/bin/env python3
"""Static contract for the compile-only rollback-confirm coordinator."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components" / "usp_ota_guard"
HEADER = (COMP / "ota_rollback_confirm_coordinator.h").read_text(encoding="utf-8")
SOURCE = (COMP / "ota_rollback_confirm_coordinator.cpp").read_text(encoding="utf-8")
COMBINED = HEADER + SOURCE
BACKEND = (
    (COMP / "ota_rollback_confirm_backend.h").read_text(encoding="utf-8")
    + (COMP / "ota_rollback_confirm_backend.cpp").read_text(encoding="utf-8")
)
GUARD = (
    (COMP / "ota_guard.h").read_text(encoding="utf-8")
    + (COMP / "ota_guard.cpp").read_text(encoding="utf-8")
    + (COMP / "__init__.py").read_text(encoding="utf-8")
)
PROD = (ROOT / "firmware" / "ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (ROOT / "firmware" / "ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    need(CURRENT_FIRMWARE_MARKER in PUBLIC, "wrong public version")
    for token in (
        "OtaRollbackConfirmCoordinator",
        "RollbackConfirmCoordinatorSnapshot",
        "policy->evaluate",
        "RollbackHealthEligibility::ALREADY_VALID",
        "RollbackHealthEligibility::ELIGIBLE_TO_CONFIRM",
        "backend->confirm",
        "backend_attempted",
        "already_valid",
    ):
        need(token in COMBINED, f"missing coordinator token: {token}")

    eval_pos = SOURCE.find("policy->evaluate")
    already_pos = SOURCE.find("RollbackHealthEligibility::ALREADY_VALID", eval_pos)
    eligible_pos = SOURCE.find("RollbackHealthEligibility::ELIGIBLE_TO_CONFIRM", already_pos)
    backend_pos = SOURCE.find("backend->confirm", eligible_pos)
    need(-1 not in (eval_pos, already_pos, eligible_pos, backend_pos),
         "coordinator ordering tokens missing")
    need(eval_pos < already_pos < eligible_pos < backend_pos,
         "ordering must be health -> already-valid -> eligible -> backend")

    forbidden = (
        "esp_ota_mark_app_valid_cancel_rollback",
        "esp_ota_mark_app_invalid_rollback",
        "esp_ota_mark_app_invalid_rollback_and_reboot",
        "esp_ota_set_boot_partition",
        "esp_ota_begin",
        "esp_ota_write",
        "esp_ota_end",
        "esp_ota_abort",
        "esp_partition_write",
        "esp_partition_write_raw",
        "esp_partition_erase_range",
        "esp_restart",
    )
    leaked = [token for token in forbidden if token in COMBINED]
    need(not leaked, f"coordinator contains direct mutation APIs: {leaked}")

    need('bool runtime_enabled_{false};' in BACKEND,
         "confirm backend is not hard-disabled")
    for setter in ("set_runtime", "enable_runtime", "runtime_enabled_ = true"):
        need(setter not in BACKEND, f"confirm backend exposes enable path: {setter}")

    for token in ("ota_rollback_confirm_coordinator", "OtaRollbackConfirmCoordinator"):
        need(token not in GUARD, f"coordinator wired into runtime guard: {token}")
        need(token not in PUBLIC, f"coordinator leaked into public profile: {token}")

    global_instance = re.search(
        r"(?m)^\s*(?:static\s+)?OtaRollbackConfirmCoordinator\s+[A-Za-z_]\w*\s*[;{(]",
        SOURCE,
    )
    need(global_instance is None, "global/static rollback coordinator instance exists")
    print(
        "OTA_ROLLBACK_CONFIRM_COORDINATOR_STATIC=PASS health_first=1 "
        "already_valid_bypass=1 eligible_only=1 direct_mutation=0 "
        "backend_hard_disabled=1 unwired=1 public_isolated=1 no_reboot=1"
    )


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"OTA_ROLLBACK_CONFIRM_COORDINATOR_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
