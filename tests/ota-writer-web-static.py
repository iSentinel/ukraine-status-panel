#!/usr/bin/env python3
"""Validate authenticated dry-run OTA writer Web transport."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "components/usp_ota_guard/ota_guard.cpp").read_text(encoding="utf-8")
H = (ROOT / "components/usp_ota_guard/ota_guard.h").read_text(encoding="utf-8")
PROD = (ROOT / "firmware/ukraine-status-panel.yaml").read_text(encoding="utf-8")
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text(encoding="utf-8")


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def section(start: str, end: str) -> str:
    need(start in CPP and end in CPP, f"section markers missing: {start} / {end}")
    return CPP.split(start, 1)[1].split(end, 1)[0]


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    for token in (
        '/usp/ota/preflight/write', 'PREFLIGHT_WRITER_PATH',
        'X-USP-OTA-Write-Permit', 'X-USP-OTA-Session',
        'X-USP-OTA-Offset', 'X-USP-OTA-Final',
        'MAX_WEB_IMAGE_CHUNK_BYTES', 'handle_writer_request_',
        'begin_writer_dry_run', 'add_writer_dry_run_chunk',
        'finish_writer_dry_run', 'BAD_WRITE_PERMIT_RATE_LIMIT = 10',
        'BAD_WRITE_PERMIT_RATE_PERIOD_MS = 30U * 1000U'):
        need(token in CPP + H, f"writer Web contract missing: {token}")

    can_handle = section('bool OtaGuard::canHandle', 'void OtaGuard::handleBody')
    need('route_is_(request, PREFLIGHT_WRITER_PATH)' in can_handle,
         'writer route is not explicitly handled')
    need('request->contentLength() > MAX_WEB_IMAGE_CHUNK_BYTES' in can_handle,
         'writer request body is not bounded')
    need('content_type_is_(request, IMAGE_CONTENT_TYPE)' in can_handle,
         'writer content type is not constrained')
    need('request->hasHeader(IMAGE_OFFSET_HEADER)' in can_handle and
         'request->hasHeader(IMAGE_FINAL_HEADER)' in can_handle,
         'writer framing headers are not required')

    body = section('void OtaGuard::handleBody', 'void OtaGuard::handle_manifest_request_')
    need('route_is_(request, PREFLIGHT_WRITER_PATH)' in body,
         'writer body branch missing')
    need('!this->session_token_matches_(request)' in body,
         'writer stream is not session-bound')
    need('write_permit_matches_(permit.value())' in body,
         'first writer request does not verify permit')
    need(body.find('write_permit_matches_(permit.value())') <
         body.find('begin_writer_dry_run(permit.value())'),
         'writer begins before permit verification')
    need('writer_new' in body and 'writer_active' in body,
         'writer first/subsequent request states are not separated')
    need('offset == this->writer_bytes_seen_' in body,
         'writer request offset is not bound to current transaction')
    need('request_end <= expected' in body,
         'writer request may overflow signed image size')
    need('request_end == expected' in body and 'request_end < expected' in body,
         'writer final flag is not exact-size bound')
    need('this->touch_session_();' in body,
         'writer traffic does not refresh session idle timer')
    need('bad_write_permit_rate_' in body,
         'invalid writer permits are not rate limited')

    handler = section('void OtaGuard::handle_writer_request_',
                      'void OtaGuard::handleRequest')
    for token in ('invalid_session', 'write_permit_missing_or_expired',
                  'invalid_write_permit', 'writer_json',
                  'finish_writer_dry_run', 'send_rate_limited_'):
        need(token in handler, f"writer response contract missing: {token}")
    need('this->send_json_(request, 202' in handler,
         'non-final writer chunk does not return accepted state')
    need('this->send_json_(request, 200' in handler,
         'finalized dry-run writer does not return success')

    session = section('bool OtaGuard::session_expired_', 'void OtaGuard::expire_session_if_needed_')
    need('WriterState::PREPARED' in session and 'WriterState::WRITING' in session,
         'active writer is not covered by session idle expiry')

    need('usp_ota_guard' not in PUBLIC and '/usp/ota/preflight/write' not in PUBLIC,
         'private writer transport leaked into public first-flash')
    forbidden = (
        'esp_ota_begin', 'esp_ota_write', 'esp_ota_end',
        'esp_ota_set_boot_partition', 'esp_restart', 'App.safe_reboot',
        '"/update"',
    )
    leaked = [token for token in forbidden if token in CPP + H]
    need(not leaked, f"dry-run transport crossed no-flash boundary: {leaked}")
    print('OTA_WRITER_WEB_STATIC=PASS auth=1 chunk_max=16384 permit_once=1 no_flash=1')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as exc:
        print(f'OTA_WRITER_WEB_STATIC=FAIL {exc}', file=sys.stderr)
        raise SystemExit(1)
