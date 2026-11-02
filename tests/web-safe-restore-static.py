#!/usr/bin/env python3
"""Validate bounded allowlist-only safe restore behavior."""
from pathlib import Path
from web_ui_static_source import read_source
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys
ROOT=Path(__file__).resolve().parents[1]
PROD=(ROOT/'firmware/ukraine-status-panel.yaml').read_text()
JS = read_source(ROOT / "firmware/webui/usp-web-ui.src.js")
def need(v,m):
    if not v: raise AssertionError(m)
def main():
    need(CURRENT_FIRMWARE_MARKER in PROD,'wrong version')
    for x in ('f.size>16384','schema!=="usp-safe-config-v1"','restorePlan(d)','loadRestore(f,t)','applyRestore(t)','network staged'):
        need(x in JS,f'missing restore contract: {x}')
    method=JS.split('restorePlan(d)',1)[1].split('setupPanel(t)',1)[0]
    for x in ('UI Language','Alert Palette','Selected Region','Outage Group Code','Panel Sounds Enabled','Network IPv4 Mode','Network Static IP','Network Gateway','Network Subnet','Network DNS 1','Network DNS 2','Starlink Enabled','Starlink Host','Starlink Port','SvitloBot Enabled'):
        need(x in method,f'missing allowlist: {x}')
    for x in ('Network Password','Network SSID','New Web Password','SvitloBot Channel Key','Apply Network Settings'):
        need(x not in method,f'forbidden restore field/action: {x}')
    need('Starlink GPS Inhibit' not in method,'GPS inhibit must not restore')
    need('Weather Location' not in method,'weather coordinates must not restore')
    need('e.option||[]' in method,'select options not validated')
    need('this.ipv4(z)' in method,'IPv4 fields not validated')
    print('WEB_SAFE_RESTORE_STATIC=PASS')
if __name__=='__main__':
    try: main()
    except AssertionError as e:
        print(f'WEB_SAFE_RESTORE_STATIC=FAIL {e}',file=sys.stderr);raise SystemExit(1)
