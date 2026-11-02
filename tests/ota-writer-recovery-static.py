#!/usr/bin/env python3
"""Validate authenticated writer-only abort/recovery invariants."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / 'components/usp_ota_guard/ota_guard.cpp').read_text(encoding='utf-8')
H = (ROOT / 'components/usp_ota_guard/ota_guard.h').read_text(encoding='utf-8')
PROD = (ROOT / 'firmware/ukraine-status-panel.yaml').read_text(encoding='utf-8')
PUBLIC = (ROOT / 'firmware/ukraine-status-panel-public-first-flash.yaml').read_text(encoding='utf-8')


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def section(start: str, end: str) -> str:
    need(start in CPP and end in CPP, f'missing section markers: {start} / {end}')
    return CPP.split(start, 1)[1].split(end, 1)[0]


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, 'wrong production version')
    for token in (
        '/usp/ota/preflight/write-abort', 'PREFLIGHT_WRITER_ABORT_PATH',
        'handle_writer_abort_request_', 'WRITE_ABORT_RATE_LIMIT = 5',
        'WRITE_ABORT_RATE_PERIOD_MS = 30U * 1000U',
        'retry_requires_abort', 'retry_requires_fresh_permit'):
        need(token in CPP + H, f'recovery contract missing: {token}')

    can_handle = section('bool OtaGuard::canHandle', 'void OtaGuard::handleBody')
    need('route_is_(request, PREFLIGHT_WRITER_ABORT_PATH)' in can_handle,
         'abort route is not registered')
    need('request->contentLength() == 0' in can_handle,
         'abort route does not require empty body')

    abort = section('void OtaGuard::handle_writer_abort_request_',
                    'void OtaGuard::handleRequest')
    need('session_token_matches_(request)' in abort,
         'abort is not candidate-session-bound')
    need('candidate_state_ != CandidateState::VERIFIED' in abort,
         'abort does not require VERIFIED candidate')
    for state in ('WriterState::PREPARED', 'WriterState::WRITING',
                  'WriterState::FAILED', 'WriterState::ABORTED'):
        need(state in abort, f'abort state contract missing: {state}')
    need('WriterState::FINALIZED' not in abort,
         'finalized writer should not be explicitly abortable')
    need(abort.find('clear_write_permit_();') < abort.find('abort_writer_dry_run();'),
         'write permit is not cleared before writer abort')
    need('clear_commit_arm_();' in abort,
         'stale commit arm is not cleared on writer abort')
    need('touch_session_();' in abort, 'successful abort does not refresh owner session')
    for forbidden in ('abort_candidate();', 'reset_candidate();', 'clear_session_token_();'):
        need(forbidden not in abort, f'writer-only abort destroys candidate/session: {forbidden}')

    cleanup = section('void OtaGuard::abort_writer_dry_run()',
                      'bool OtaGuard::begin_writer_dry_run')
    for token in ('writer_sha_active_', 'writer_bytes_seen_ = 0',
                  'writer_expected_size_ = 0', 'writer_expected_sha256_.clear()',
                  'writer_actual_sha256_.clear()', 'WriterState::ABORTED'):
        need(token in cleanup, f'abort cleanup missing: {token}')

    body = section('void OtaGuard::handleBody', 'void OtaGuard::handle_manifest_request_')
    retry_expr = ('this->writer_state_ == WriterState::IDLE ||\n'
                  '                              this->writer_state_ == WriterState::ABORTED')
    need(retry_expr in body, 'ABORTED state is not retryable as a new writer transaction')
    need('write_permit_matches_(permit.value())' in body,
         'retry does not require a fresh write permit')

    writer_json = section('std::string writer_json', 'bool parse_decimal_u32')
    need('retry_requires_abort' in writer_json and
         'retry_requires_fresh_permit' in writer_json,
         'recovery requirements are not diagnosable')

    need('usp_ota_guard' not in PUBLIC and '/usp/ota/preflight/write-abort' not in PUBLIC,
         'private recovery API leaked into public first-flash')
    forbidden_flash = ('esp_ota_begin', 'esp_ota_write', 'esp_ota_end',
                       'esp_ota_set_boot_partition', 'esp_restart', 'App.safe_reboot')
    leaked = [token for token in forbidden_flash if token in CPP + H]
    need(not leaked, f'recovery stage crossed no-flash boundary: {leaked}')
    print('OTA_WRITER_RECOVERY_STATIC=PASS preserve_candidate=1 fresh_permit=1 abort_rate=5/30s no_flash=1')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as exc:
        print(f'OTA_WRITER_RECOVERY_STATIC=FAIL {exc}', file=sys.stderr)
        raise SystemExit(1)
