#!/usr/bin/env python3
"""Require page-memory consent before every external Kyiv address request."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / "firmware/webui/usp-web-ui.src.js").read_text()


def main():
    """Verify consent exists before UI fields and before the fetch helper."""
    required = (
        'this.__uspAddressLookupAllowed = false;',
        'External address lookup consent required',
        'usp-external-consent',
        'this.__uspAddressLookupAllowed = true; this.queue()',
        't.externalPrivacy',
        't.enableExternalLookup',
        'https://getnubergroupproxy.onrender.com',
    )
    for token in required:
        assert token in SRC, token
    finder = SRC[SRC.index('kyivFinderPanel(t)'):SRC.index('__uspCreateAddressField')]
    assert finder.index('if (!this.__uspAddressLookupAllowed)') < finder.index('usp-finder-fields')
    consent = finder[finder.index('if (!this.__uspAddressLookupAllowed)'):finder.index('usp-finder-fields')]
    assert 'localStorage' not in consent and 'sessionStorage' not in consent
    getter = SRC[SRC.index('async gj(path)'):SRC.index('er(message)')]
    assert getter.index('__uspAddressLookupAllowed') < getter.index('fetch(')
    print('FW11_ADDRESS_PRIVACY_STATIC=PASS consent_before_fetch=1 persistence=page-memory-only')


if __name__ == '__main__':
    main()
