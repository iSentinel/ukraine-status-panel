#!/usr/bin/env python3
"""Validate fail-closed RAM-only throttling for private OTA preflight."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT/'components/usp_ota_guard/ota_guard.cpp').read_text()
H = (ROOT/'components/usp_ota_guard/ota_guard.h').read_text()
PROD = (ROOT/'firmware/ukraine-status-panel.yaml').read_text()
PUBLIC = (ROOT/'firmware/ukraine-status-panel-public-first-flash.yaml').read_text()

def need(ok,msg):
    if not ok: raise AssertionError(msg)

def section(start,end):
    return CPP.split(start,1)[1].split(end,1)[0]

def main():
    need(CURRENT_FIRMWARE_MARKER in PROD,'wrong version')
    for token in (
        'MANIFEST_RATE_LIMIT = 5','MANIFEST_RATE_PERIOD_MS = 60U * 1000U',
        'RESET_RATE_LIMIT = 5','RESET_RATE_PERIOD_MS = 30U * 1000U',
        'BAD_SESSION_RATE_LIMIT = 10','BAD_SESSION_RATE_PERIOD_MS = 30U * 1000U',
        'struct RateWindow','allow_rate_','send_rate_limited_','Retry-After',
        'retry_after_ms','request_rate_limited_'):
        need(token in H+CPP,f'missing rate contract: {token}')
    allow = section('bool OtaGuard::allow_rate_','bool OtaGuard::create_session_token_')
    need('now - window.started_ms >= period_ms' in allow,'rate window not wrap-safe')
    need('window.count >= limit' in allow and '++window.count' in allow,'fixed-window accounting missing')
    manifest = section('void OtaGuard::handle_manifest_request_','void OtaGuard::handle_image_request_')
    need(manifest.find('allow_rate_') < manifest.find('begin_candidate('),'manifest crypto/mutation occurs before limiter')
    reset = section('void OtaGuard::handle_reset_request_','void OtaGuard::handleRequest')
    need(reset.find('allow_rate_') < reset.find('reset_candidate()'),'reset mutation occurs before limiter')
    image_body = section('void OtaGuard::handleBody','void OtaGuard::handle_manifest_request_')
    need('this->request_session_error_' in image_body and 'this->bad_session_rate_' in image_body,
         'wrong-session image path not throttled')
    need('this->touch_session_();' in image_body,'valid image progress missing')
    image_reply = section('void OtaGuard::handle_image_request_','void OtaGuard::handle_status_request_')
    rate_branch = image_reply.split('if (this->request_rate_limited_)',1)[1].split('if (this->request_session_error_)',1)[0]
    need('abort_candidate' not in rate_branch and 'touch_session_' not in rate_branch,
         'rate-limited image request mutates candidate/session')
    status = section('void OtaGuard::handle_status_request_','void OtaGuard::handle_reset_request_')
    need('allow_rate_' not in status and 'touch_session_' not in status,'status should stay cheap and not extend TTL')
    need('/usp/ota/preflight/' not in PUBLIC and 'usp_ota_guard' not in PUBLIC,'public leak')
    forbidden=('esp_ota_begin','esp_ota_write','esp_ota_end','esp_ota_set_boot_partition','esp_restart','App.safe_reboot','"/update"')
    need(not [x for x in forbidden if x in CPP+H],'rate stage crossed no-write boundary')
    print('OTA_RATE_LIMIT_STATIC=PASS manifest=5/60s reset=5/30s bad_session=10/30s')

if __name__=='__main__':
    try: main()
    except AssertionError as exc:
        print(f'OTA_RATE_LIMIT_STATIC=FAIL {exc}',file=sys.stderr); raise SystemExit(1)
