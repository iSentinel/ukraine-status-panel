#!/usr/bin/env python3
"""Validate staged section resets and protected NVS factory reset."""
from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER
import sys

ROOT=Path(__file__).resolve().parents[1]
PROD=(ROOT/'firmware/ukraine-status-panel.yaml').read_text()
PUBLIC=(ROOT/'firmware/ukraine-status-panel-public-first-flash.yaml')
JS=(ROOT/'firmware/webui/usp-web-ui.src.js').read_text()

def need(ok,msg):
    if not ok: raise AssertionError(msg)

def section(start,end):
    return PROD.split(start,1)[1].split(end,1)[0]

def main():
    need(CURRENT_FIRMWARE_MARKER in PROD,'wrong version')
    need('id: web_factory_reset_arm_until_ms\n    type: uint32_t\n    restore_value: no' in PROD,'factory reset arm must be non-persistent')
    phrase=section('id: web_factory_reset_phrase','id: web_starlink_host')
    need('x == "FACTORY RESET"' in phrase,'exact reset phrase missing')
    need('millis() + 30000U' in phrase,'30 second arm window missing')
    reset=section('# WEB_RUNTIME_BEGIN: protected-reset-buttons','# WEB_RUNTIME_END: protected-reset-buttons')
    for token in ('id: web_reset_network_button','name: "Reset Network"','id: web_reset_integrations_button','name: "Reset Integrations"','platform: factory_reset','id: web_factory_reset_internal','internal: true','id: web_factory_reset_button','name: "Factory Reset"','id(web_factory_reset_internal).press()'):
        need(token in reset,f'missing reset contract: {token}')
    need('static_cast<int32_t>(until - millis()) <= 0' in reset,'factory expiry gate missing')
    net=reset.split('id: web_reset_network_button',1)[1].split('id: web_reset_integrations_button',1)[0]
    for token in ('set_option("DHCP")','web_network_ssid','web_network_password','web_network_static_ip','web_network_gateway','web_network_subnet','web_network_dns1','web_network_dns2','set_value("")'):
        need(token in net,f'network reset missing: {token}')
    for forbidden in ('web_network_apply_button','Apply Network Settings','wifi_sta_connect','safe_reboot'):
        need(forbidden not in net,f'network reset must remain staged: {forbidden}')
    integ=reset.split('id: web_reset_integrations_button',1)[1].split('platform: factory_reset',1)[0]
    for token in ('web_svitlobot_enabled).turn_off()','web_svitlobot_key','key.set_value("")','web_starlink_enabled).turn_off()','host.set_value("192.168.100.1")','port.set_value(9200.0f)'):
        need(token in integ,f'integration reset missing: {token}')
    need('web_starlink_gps_inhibit' not in integ,'integration reset must not mutate Dishy GPS inhibit')
    for row in ('["Reset Network","Скинути мережу","Сбросить сеть"]','["Reset Integrations","Скинути інтеграції","Сбросить интеграции"]','["Factory Reset Phrase","Фраза скидання","Фраза сброса"]','["Factory Reset Status","Стан скидання","Статус сброса"]','["Factory Reset","Повне скидання","Полный сброс"]'):
        need(row in JS,f'localized reset label missing: {row}')
    need(PUBLIC.exists(),'public YAML missing')
    public=PUBLIC.read_text()
    for token in ('web_factory_reset','web_reset_network_button','web_reset_integrations_button','Factory Reset Phrase','Factory Reset Status','name: "Factory Reset"'):
        need(token not in public,f'public bootstrap leaked reset runtime: {token}')
    print('WEB_RESET_FLOWS_STATIC=PASS')

if __name__=='__main__':
    try: main()
    except AssertionError as exc:
        print(f'WEB_RESET_FLOWS_STATIC=FAIL {exc}',file=sys.stderr); raise SystemExit(1)
