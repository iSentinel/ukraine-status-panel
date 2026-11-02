#!/usr/bin/env python3
"""Guard per-candidate Web preflight session ownership without flash writes."""
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


def main() -> None:
    need(CURRENT_FIRMWARE_MARKER in PROD, "wrong production version")
    for token in (
        'SESSION_TOKEN_BYTES = 16', 'X-USP-OTA-Session', 'esp_fill_random',
        'create_session_token_', 'session_token_matches_', 'clear_session_token_',
        'response->addHeader(SESSION_HEADER', 'sodium_memcmp', 'sodium_memzero',
        'request_session_error_', 'invalid_session',
    ):
        need(token in CPP or token in H, f"session contract missing: {token}")
    need('if (!this->request_session_error_) this->abort_candidate();' in CPP,
         "wrong/missing session may abort another client's candidate")
    need('this->request_session_error_ = !this->session_token_matches_(request);' in CPP,
         "image stream is not session-bound before mutation")
    need('this->session_token_active_ && !this->session_token_matches_(request)' in CPP,
         "active candidate reset is not owner-bound")
    need('accepted && !this->create_session_token_()' in CPP,
         "session token is not created only after accepted manifest")
    need('this->clear_session_token_();\n  this->reset_image_preflight();' in CPP,
         "explicit candidate reset does not zero session ownership")
    json_block = CPP.split('std::string candidate_json', 1)[1].split('bool parse_decimal_u32', 1)[0]
    need('session_token_hex_' not in json_block and 'SESSION_HEADER' not in json_block,
         "session secret leaked in status JSON")
    need('/usp/ota/preflight/' not in PUBLIC and 'usp_ota_guard' not in PUBLIC,
         "public first-flash leaked session-bound private OTA surface")
    forbidden = ('esp_ota_begin', 'esp_ota_write', 'esp_ota_end',
                 'esp_ota_set_boot_partition', 'esp_restart', 'App.safe_reboot', '"/update"')
    need(not [item for item in forbidden if item in CPP + H],
         "session hardening crossed the no-write/no-reboot boundary")
    print('OTA_WEB_SESSION_STATIC=PASS token_bits=128 owner_bound=1 no_writer=1')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as exc:
        print(f'OTA_WEB_SESSION_STATIC=FAIL {exc}', file=sys.stderr)
        raise SystemExit(1)
