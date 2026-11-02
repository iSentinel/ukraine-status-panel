#!/usr/bin/env python3
"""Validate the dry-run OTA writer transaction and no-flash boundary."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / 'components/usp_ota_guard/ota_guard.cpp').read_text()
H = (ROOT / 'components/usp_ota_guard/ota_guard.h').read_text()
PROD = (ROOT / 'firmware/ukraine-status-panel.yaml').read_text()
PUBLIC = (ROOT / 'firmware/ukraine-status-panel-public-first-flash.yaml').read_text()


def need(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def section(start: str, end: str) -> str:
    return CPP.split(start, 1)[1].split(end, 1)[0]


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, 'wrong version')
    for token in (
        'enum class WriterState', 'PREPARED', 'WRITING', 'FINALIZED', 'FAILED',
        'enum class WriterFailure', 'OFFSET_MISMATCH', 'SIZE_OVERFLOW',
        'SIZE_MISMATCH', 'SHA256_MISMATCH', 'WriterSnapshot',
        'WRITE_PERMIT_TOKEN_BYTES = 16', 'WRITE_PERMIT_LIFETIME_MS = 30U * 1000U',
        'X-USP-OTA-Write-Permit', 'create_write_permit_', 'write_permit_matches_',
        'clear_write_permit_', 'begin_writer_dry_run', 'add_writer_dry_run_chunk',
        'finish_writer_dry_run', 'abort_writer_dry_run', 'reset_writer_dry_run'):
        need(token in H + CPP, f'missing writer contract: {token}')

    permit = section('bool OtaGuard::create_write_permit_',
                     'bool OtaGuard::write_permit_matches_')
    need('CandidateState::VERIFIED' in permit and 'session_token_active_' in permit,
         'write permit is not bound to verified session')
    need('esp_fill_random' in permit, 'write permit is not random')

    begin = section('bool OtaGuard::begin_writer_dry_run',
                    'bool OtaGuard::add_writer_dry_run_chunk')
    need(begin.find('write_permit_matches_') < begin.find('clear_write_permit_'),
         'permit is cleared before verification')
    need(begin.find('clear_write_permit_') < begin.find('writer_state_ = WriterState::PREPARED'),
         'permit is not consumed before transaction preparation')
    need('candidate_manifest_.image_size' in begin and
         'candidate_manifest_.image_sha256' in begin,
         'writer is not bound to signed candidate expectations')
    need('mbedtls_sha256_starts' in begin, 'writer hash context not initialized')

    chunk = section('bool OtaGuard::add_writer_dry_run_chunk',
                    'bool OtaGuard::finish_writer_dry_run')
    need('offset != this->writer_bytes_seen_' in chunk,
         'writer does not enforce sequential offsets')
    need('SIZE_OVERFLOW' in chunk and 'mbedtls_sha256_update' in chunk,
         'writer chunk size/hash checks missing')

    finish = section('bool OtaGuard::finish_writer_dry_run',
                     'WriterSnapshot OtaGuard::writer_snapshot')
    need('writer_bytes_seen_ != this->writer_expected_size_' in finish,
         'writer can finalize short stream')
    need('writer_actual_sha256_ != this->writer_expected_sha256_' in finish,
         'writer can finalize wrong digest')
    need('WriterState::FINALIZED' in finish, 'writer final state missing')

    commit = section('void OtaGuard::handle_commit_check_request_',
                     'void OtaGuard::handleRequest')
    need(commit.find('clear_commit_arm_();') < commit.find('create_write_permit_'),
         'write permit is not downstream of consumed commit arm')
    need('send_json_with_write_permit_' in commit and 'writes_flash\\\":false' in commit,
         'commit-check does not return dry-run write permit')

    clear_session = section('void OtaGuard::clear_session_token_',
                            'bool OtaGuard::create_commit_arm_')
    need('clear_write_permit_' in clear_session and 'reset_writer_dry_run' in clear_session,
         'session clear does not invalidate writer authorization/state')
    abort = section('void OtaGuard::abort_candidate()', 'bool OtaGuard::begin_candidate')
    need('clear_write_permit_' in abort and 'abort_writer_dry_run' in abort,
         'candidate abort does not invalidate writer transaction')

    status = section('void OtaGuard::handle_status_request_',
                     'void OtaGuard::handle_reset_request_')
    for token in ('write_permit_active', 'write_permit_remaining_ms',
                  'writer_state', 'writer_failure', 'writer_bytes_seen',
                  'writer_expected_bytes'):
        need(token in status, f'status missing {token}')
    need('write_permit_token_hex_' not in status and 'WRITE_PERMIT_HEADER' not in status,
         'status leaks write permit')

    need('/usp/ota/preflight/write' in CPP,
         'dry-run writer Web transport missing in .165')
    need('usp_ota_guard' not in PUBLIC and 'X-USP-OTA-Write-Permit' not in PUBLIC,
         'writer transaction leaked into public first-flash')
    forbidden = ('esp_ota_begin', 'esp_ota_write', 'esp_ota_end',
                 'esp_ota_set_boot_partition', 'esp_restart', 'App.safe_reboot', '"/update"')
    leaked = [item for item in forbidden if item in CPP + H]
    need(not leaked, f'no-flash boundary crossed: {leaked}')
    print('OTA_WRITER_TRANSACTION_STATIC=PASS permit_bits=128 dry_run=1 no_flash=1')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as exc:
        print(f'OTA_WRITER_TRANSACTION_STATIC=FAIL {exc}', file=sys.stderr)
        raise SystemExit(1)
