#!/usr/bin/env python3
"""Guard the authenticated, no-write Web OTA preflight transport."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "components/usp_ota_guard/ota_guard.cpp").read_text(encoding="utf-8")
H = (ROOT / "components/usp_ota_guard/ota_guard.h").read_text(encoding="utf-8")
PYCOMP = (ROOT / "components/usp_ota_guard/__init__.py").read_text(encoding="utf-8")
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    need('DEPENDENCIES = ["web_server"]' in PYCOMP, "web_server dependency missing")
    need('global_web_server_base->add_handler(this)' in CPP,
         "preflight handler is not registered through authenticated WebServerBase")
    need('global_web_server_base->add_handler_without_auth' not in CPP,
         "private preflight route bypasses authentication")
    need('#ifdef USE_WEBSERVER_AUTH' in CPP, "route does not fail closed without Web auth")
    for token in (
        "/usp/ota/preflight/status", "/usp/ota/preflight/manifest",
        "/usp/ota/preflight/image", "/usp/ota/preflight/reset",
        "X-USP-OTA-Signature", "X-USP-OTA-Key-Id",
        "X-USP-OTA-Offset", "X-USP-OTA-Final", "X-USP-OTA-Session",
        "MAX_WEB_IMAGE_CHUNK_BYTES",
        "application/vnd.usp.ota-manifest+json", "application/octet-stream",
        "ED25519_SIGNATURE_B64_LENGTH", "MAX_MANIFEST_BYTES",
        "begin_candidate", "add_candidate_chunk", "finish_candidate",
        "candidate_snapshot", "writes_flash\\\":false",
    ):
        need(token in CPP or token in H, f"Web preflight contract missing: {token}")
    need('ESPHOME_PROJECT_VERSION, false' in CPP,
         "Web preflight must deny downgrade by default using installed project version")
    need("request->contentLength() > MAX_WEB_IMAGE_CHUNK_BYTES" in CPP,
         "unauthenticated raw-body exposure is not bounded to 16 KiB")
    need("offset != this->image_bytes_seen_" in CPP,
         "image chunk ordering is not bound to current stream offset")
    need("this->request_image_final_" in CPP and
         "this->image_bytes_seen_ + total != this->candidate_manifest_.image_size" in CPP,
         "final chunk is not bound to signed manifest size")
    need('signature_b64.value().size() != ED25519_SIGNATURE_B64_LENGTH' in CPP,
         "signature header is not length bounded")
    need('ota: false' in PROD, "stock browser OTA must remain disabled")
    need('/usp/ota/preflight/' not in PUBLIC and 'usp_ota_guard' not in PUBLIC,
         "public first-flash leaked private preflight surface")
    forbidden = (
        "esp_ota_begin", "esp_ota_write", "esp_ota_end",
        "esp_ota_set_boot_partition", "esp_restart", "App.safe_reboot",
        '"/update"', "ota_backend_",
    )
    combined = CPP + H
    leaked = [token for token in forbidden if token in combined]
    need(not leaked, f"preflight transport crossed no-write/no-reboot boundary: {leaked}")
    print("OTA_WEB_PREFLIGHT_STATIC=PASS authenticated=1 streaming=1 no_writer=1")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"OTA_WEB_PREFLIGHT_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
