#!/usr/bin/env python3
"""Guard the ordered single-candidate OTA preflight session boundary."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
H = (ROOT / "components/usp_ota_guard/ota_guard.h").read_text(encoding="utf-8")
CPP = (ROOT / "components/usp_ota_guard/ota_guard.cpp").read_text(encoding="utf-8")
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    for token in (
        "CandidateState", "MANIFEST_ACCEPTED", "RECEIVING_IMAGE", "VERIFIED",
        "FAILED", "ABORTED", "CandidateFailure", "NOT_IDLE", "INVALID_STATE",
        "MANIFEST_REJECTED", "IMAGE_INIT_FAILED", "CHUNK_REJECTED",
        "IMAGE_REJECTED", "begin_candidate", "add_candidate_chunk",
        "finish_candidate", "abort_candidate", "reset_candidate",
        "candidate_snapshot", "validate_signed_manifest", "begin_image_preflight",
        "add_image_chunk", "finish_image_preflight",
    ):
        need(token in H or token in CPP, f"candidate session contract missing: {token}")
    need('candidate_state_ != CandidateState::IDLE' in CPP,
         "second begin is not blocked")
    need('CandidateFailure::NOT_IDLE' in CPP, "busy/not-idle diagnostic missing")
    begin = CPP.find("bool OtaGuard::begin_candidate")
    validate = CPP.find("this->validate_signed_manifest(", begin)
    image = CPP.find("this->begin_image_preflight", begin)
    need(begin >= 0 and validate > begin and image > validate,
         "candidate does not enforce manifest-before-image ordering")
    need('this->reset_image_preflight();' in CPP[CPP.find('void OtaGuard::abort_candidate'):],
         "abort does not clear streaming state")
    need("usp_ota_guard" not in PUBLIC, "public first-flash leaked OTA candidate session")
    forbidden = (
        "esp_ota_begin", "esp_ota_write", "esp_ota_end", "esp_ota_set_boot_partition",
        "esp_restart",
    )
    combined = H + CPP
    leaked = [token for token in forbidden if token in combined]
    need(not leaked, f"candidate session crossed no-write/no-route boundary: {leaked}")
    print("OTA_CANDIDATE_SESSION_STATIC=PASS ordered=1 single_active=1 no_writer=1")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"OTA_CANDIDATE_SESSION_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
