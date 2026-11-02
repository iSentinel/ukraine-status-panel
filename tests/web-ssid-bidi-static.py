#!/usr/bin/env python3
"""Keep SSID bidi safety display-only and preserve raw network values."""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SRC=(ROOT/'firmware/webui/usp-web-ui.src.js').read_text()


def main():
    assert 'function safeSsidDisplay(value)' in SRC
    assert r'/[\u061C\u200E\u200F\u202A-\u202E\u2066-\u2069]/g' in SRC
    assert '`${safeSsidDisplay(n.ssid)} · ${n.rssi}dBm${n.secure?" WPA":""}`' in SRC
    assert 'o.value=n.ssid' in SRC.replace(' ', '')
    assert 'r[0]==="Wi-Fi SSID"?safeSsidDisplay(v):v' in SRC
    assert 'entity.name === "Wi-Fi SSID" ? safeSsidDisplay(value) : value' in SRC
    for old in ('this.kf(', 'this.lg(', 'this.ag(', '  kf(', '  async lg(', '  async ag('):
        assert old not in SRC, old
    for new in ('kyivFinderPanel(', 'lookupGroup(', 'applyGroup(', 'wifiControl('):
        assert new in SRC, new
    print('FW10_SSID_BIDI_STATIC=PASS display_only=1 readable_names=1')


if __name__=='__main__':
    main()
