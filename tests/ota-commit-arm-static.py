#!/usr/bin/env python3
"""Validate the one-time in-RAM OTA commit-arm interlock."""
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
        'COMMIT_ARM_TOKEN_BYTES = 16',
        'COMMIT_ARM_LIFETIME_MS = 30U * 1000U',
        'COMMIT_ARM_RATE_LIMIT = 5',
        'BAD_COMMIT_ARM_RATE_LIMIT = 10',
        'PREFLIGHT_COMMIT_ARM_PATH[] = "/usp/ota/preflight/arm"',
        'PREFLIGHT_COMMIT_CHECK_PATH[] = "/usp/ota/preflight/commit-check"',
        'COMMIT_ARM_HEADER[] = "X-USP-OTA-Commit-Arm"',
        'create_commit_arm_', 'commit_arm_matches_', 'commit_arm_expired_',
        'expire_commit_arm_if_needed_', 'clear_commit_arm_',
        'send_json_with_commit_arm_'):
        need(token in H + CPP, f'missing commit-arm contract: {token}')

    create = section('bool OtaGuard::create_commit_arm_', 'bool OtaGuard::commit_arm_matches_')
    need('CandidateState::VERIFIED' in create, 'arm can be created before VERIFIED')
    need('session_token_active_' in create, 'arm is not bound to active session')
    need('esp_fill_random' in create and 'commit_arm_started_ms_ = millis()' in create,
         'arm challenge is not fresh/timed')

    match = section('bool OtaGuard::commit_arm_matches_', 'bool OtaGuard::commit_arm_expired_')
    need('sodium_memcmp' in match, 'arm comparison is not constant-time')
    expire = section('bool OtaGuard::commit_arm_expired_', 'void OtaGuard::expire_commit_arm_if_needed_')
    need('now - this->commit_arm_started_ms_ >= COMMIT_ARM_LIFETIME_MS' in expire,
         'arm expiry is not wrap-safe')
    clear = section('void OtaGuard::clear_commit_arm_', 'bool OtaGuard::canHandle')
    need(clear.count('sodium_memzero') >= 2, 'arm secret is not zeroized')

    arm = section('void OtaGuard::handle_commit_arm_request_',
                  'void OtaGuard::handle_commit_check_request_')
    need(arm.find('session_token_matches_') < arm.find('CandidateState::VERIFIED'),
         'arm does not authenticate session first')
    need(arm.find('allow_rate_') < arm.find('create_commit_arm_'),
         'arm mutation happens before arm rate limiter')
    need('send_json_with_commit_arm_' in arm and 'writes_flash\\\":false' in arm,
         'arm response contract missing')

    check = section('void OtaGuard::handle_commit_check_request_', 'void OtaGuard::handleRequest')
    need(check.find('session_token_matches_') < check.find('CandidateState::VERIFIED'),
         'commit-check does not authenticate session first')
    need('bad_commit_arm_rate_' in check, 'bad arm challenge is not throttled')
    need('commit_arm_matches_' in check, 'commit-check does not verify arm')
    need(check.find('clear_commit_arm_();') < check.find('commit_authorized'),
         'valid arm is not consumed before authorization response')
    need('commit_authorized\\\":true' in check and 'writes_flash\\\":false' in check,
         'commit-check must remain dry-run')

    status = section('void OtaGuard::handle_status_request_',
                     'void OtaGuard::handle_reset_request_')
    need('commit_arm_active' in status and 'commit_arm_remaining_ms' in status,
         'status does not expose arm state')
    need('commit_arm_token_hex_' not in status and 'COMMIT_ARM_HEADER' not in status,
         'status leaks arm challenge')

    need('this->clear_commit_arm_();' in section('void OtaGuard::clear_session_token_',
                                                 'bool OtaGuard::create_commit_arm_'),
         'session clear does not invalidate arm')
    need('this->clear_commit_arm_();' in section('void OtaGuard::abort_candidate()',
                                                 'bool OtaGuard::begin_candidate'),
         'candidate abort does not invalidate arm')
    need('/usp/ota/preflight/arm' not in PUBLIC and 'usp_ota_guard' not in PUBLIC,
         'commit arm leaked into public first-flash')

    forbidden = ('esp_ota_begin', 'esp_ota_write', 'esp_ota_end',
                 'esp_ota_set_boot_partition', 'esp_restart', 'App.safe_reboot', '"/update"')
    need(not [item for item in forbidden if item in CPP + H],
         'commit-arm stage crossed no-write/no-reboot boundary')
    print('OTA_COMMIT_ARM_STATIC=PASS token_bits=128 ttl_ms=30000 one_time=1 no_writer=1')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as exc:
        print(f'OTA_COMMIT_ARM_STATIC=FAIL {exc}', file=sys.stderr)
        raise SystemExit(1)
