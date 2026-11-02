#!/usr/bin/env python3
"""Verify that JPEG support belongs only to the private firmware profile."""
import argparse
from pathlib import Path
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
FLAG = "-DLV_USE_TJPGD=1"


def private_flag(text):
    """Require the compiler flag inside the explicitly removable private block."""
    begin = "  # WEB_RUNTIME_BEGIN: spotify-artwork-decoder"
    end = "  # WEB_RUNTIME_END: spotify-artwork-decoder"
    block = text.split(begin, 1)[1].split(end, 1)[0]
    assert text.count(FLAG) == 1 and FLAG in block
    assert "build_flags:" in block


def main():
    """Check all three committed profiles without compiling or contacting a panel."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    firmware = ROOT / "firmware"
    for name in (
        "ukraine-status-panel.yaml",
        "ukraine-status-panel-production.yaml",
    ):
        private_flag((firmware / name).read_text())
    public = (
        firmware / "ukraine-status-panel-public-first-flash.yaml"
    ).read_text()
    assert FLAG not in public
    assert "usp_spotify_auth:" not in public
    logger.info(
        "SPOTIFY_ARTWORK_DECODER_STATIC=PASS private_only public_disabled"
    )


if __name__ == "__main__":
    main()
