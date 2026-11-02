#!/usr/bin/env python3
"""Validate bounded OTA candidate lifetime and idle replay hardening."""
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


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, 'wrong production version')
    for token in (
        'SESSION_MAX_LIFETIME_MS = 10U * 60U * 1000U',
        'SESSION_IDLE_TIMEOUT_MS = 90U * 1000U',
        'void loop() override', 'expire_session_if_needed_', 'session_expired_',
        'CandidateFailure::SESSION_EXPIRED', '"session_expired"',
        'now - this->session_started_ms_ >= SESSION_MAX_LIFETIME_MS',
        'now - this->session_last_activity_ms_ >= SESSION_IDLE_TIMEOUT_MS',
        'this->touch_session_();', 'sodium_memzero',
        'session_active', 'session_remaining_ms',
        'session_idle_remaining_ms',
    ):
        need(token in CPP or token in H, f'expiry contract missing: {token}')
    loop = CPP.split('void OtaGuard::loop()', 1)[1].split('void OtaGuard::dump_config', 1)[0]
    need('this->expire_session_if_needed_();' in loop,
         'expiry is not enforced without HTTP traffic')
    need('this->expire_commit_arm_if_needed_();' in loop,
         'commit-arm expiry is not enforced without HTTP traffic')
    need('this->expire_write_permit_if_needed_();' in loop,
         'write-permit expiry is not enforced without HTTP traffic')
    handle = CPP.split('void OtaGuard::handleRequest', 1)[1].split('void OtaGuard::handleBody', 1)[0]
    need('this->expire_session_if_needed_();' in handle,
         'request race is not closed before routing')
    body = CPP.split('void OtaGuard::handleBody', 1)[1].split('void OtaGuard::handle_manifest_request_', 1)[0]
    need('this->expire_session_if_needed_();' in body,
         'stream race is not closed before body mutation')
    status = CPP.split('void OtaGuard::handle_status_request_', 1)[1].split('void OtaGuard::handle_reset_request_', 1)[0]
    need('touch_session_' not in status,
         'status polling must never refresh the session deadline')
    expire = CPP.split('void OtaGuard::expire_session_if_needed_', 1)[1].split('void OtaGuard::touch_session_', 1)[0]
    need('this->abort_candidate();' in expire and 'this->clear_session_token_();' in expire,
         'expiry must terminate candidate and zero ownership')
    need('this->candidate_failure_ = CandidateFailure::SESSION_EXPIRED;' in expire,
         'expiry must remain diagnosable after abort')
    need('/usp/ota/preflight/' not in PUBLIC and 'usp_ota_guard' not in PUBLIC,
         'public bootstrap leaked private expiry/session surface')
    forbidden = ('esp_ota_begin', 'esp_ota_write', 'esp_ota_end',
                 'esp_ota_set_boot_partition', 'esp_restart', 'App.safe_reboot', '"/update"')
    need(not [x for x in forbidden if x in CPP + H],
         'expiry stage crossed no-write/no-reboot boundary')
    print('OTA_SESSION_EXPIRY_STATIC=PASS max_ms=600000 idle_ms=90000 wrap_safe=1')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as exc:
        print(f'OTA_SESSION_EXPIRY_STATIC=FAIL {exc}', file=sys.stderr)
        raise SystemExit(1)
