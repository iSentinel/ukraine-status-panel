#!/usr/bin/env python3
"""Verify Digest authentication binds challenge, route, and nonce count."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "components/web_server_idf/web_server_idf.cpp").read_text()
HEADER = (ROOT / "components/web_server_idf/digest_auth_state.h").read_text()


def require(condition: bool, message: str) -> None:
    """Fail with a concise contract message."""
    if not condition:
        raise AssertionError(message)


def main() -> None:
    """Check production wiring for A-F3 defenses."""
    require('#include "digest_auth_state.h"' in CPP, "nonce helper not included")
    require('DIGEST_REALM = "Login Required"' in CPP, "realm not fixed")
    require('DIGEST_QOP = "auth"' in CPP, "qop not fixed")
    require('digest_param(params, "opaque")' in CPP, "opaque not checked")
    require("this->req_->uri" in CPP, "actual request URI not bound")
    require("digest_nonce_store.issue" in CPP, "issued nonce not stored")
    require("digest_nonce_store.consume" in CPP, "nonce count not consumed")
    require("parse_nonce_count" in CPP, "nonce count not parsed")
    require("NONCE_TTL_MS = 5U * 60U * 1000U" in HEADER, "TTL changed")
    require("NONCE_CAPACITY = 12" in HEADER, "nonce ring not bounded")
    require("NC_WINDOW_BITS = 64" in HEADER, "reorder window missing")
    require("nonce we issued is not tracked" not in CPP, "stateless verifier remains")
    print(
        "DIGEST_AUTH_REPLAY_STATIC=PASS realm=qop=uri opaque=1 "
        "nonce_ring=12 ttl=5min nc_replay=blocked"
    )


if __name__ == "__main__":
    main()
