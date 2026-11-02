#!/usr/bin/env python3
"""Check private Spotify page generation and unavailable-worker admission."""

import argparse
import ast
import importlib.util
from pathlib import Path
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def generator_checks():
    """Exercise deterministic compression, content round-trip and budget failure."""
    script = ROOT / "scripts/build-spotify-auth-assets.py"
    spec = importlib.util.spec_from_file_location("spotify_assets", script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    source = ROOT / "firmware/webui/spotify-auth"
    channel = ROOT / "firmware/webui/usp-spotify-auth-return.js"
    first, sizes = module.generated(source, channel)
    assert module.generated(source, channel)[0] == first
    assert first == (ROOT / "components/usp_spotify_auth/assets.h").read_text()
    assert sum(item["gzip"] for item in sizes.values()) <= module.BUDGET
    with tempfile.TemporaryDirectory(prefix="usp-spotify-assets-") as folder:
        destination = Path(folder)
        for filename in module.FILES:
            if filename != "return.js":
                (destination / filename).write_bytes(
                    (source / filename).read_bytes()
                )
        (destination / "page.css").unlink()
        try:
            module.generated(destination, channel)
            raise AssertionError("Missing asset accepted")
        except FileNotFoundError:
            pass
    module.BUDGET = 1
    try:
        module.generated(source, channel)
        raise AssertionError("Oversized assets accepted")
    except ValueError:
        pass


def source_checks():
    """Keep language on the main task and sign-in disabled until a worker exists."""
    source = (
        ROOT / "components/usp_spotify_auth/spotify_auth.cpp"
    ).read_text()
    header = (ROOT / "components/usp_spotify_auth/spotify_auth.h").read_text()
    schema = (ROOT / "components/usp_spotify_auth/__init__.py").read_text()
    page = (ROOT / "firmware/webui/spotify-auth/page.js").read_text()
    relay = (ROOT / "web-installer/spotify-callback/relay.js").read_text()
    assert "std::atomic<bool> exchange_available_{false}" in header
    assert "std::atomic<uint8_t> cached_language_" in header
    assert "this->cached_language_.store" in source
    assert "exchange_unavailable" in source
    assert "set_exchange_available" not in schema
    assert "cg.process_lambda" in schema
    assert "EventSource" not in page and "localStorage" not in page
    assert (
        "sessionStorage" not in page and "set_exchange_available" not in page
    )
    assert "frame-ancestors 'none'" in source and "Content-Encoding" in source
    assert "beginResponse(200, type, data, size)" in source
    assert "manualButton.addEventListener" in relay
    assert (
        'location.href="/usp/spotify/auth/"'
        in (ROOT / "firmware/webui/usp-web-ui.src.js").read_text()
    )
    for name in ("scripts/build-spotify-auth-assets.py", __file__):
        tree = ast.parse((ROOT / name).read_text())
        for function in ast.walk(tree):
            if isinstance(function, (ast.FunctionDef, ast.AsyncFunctionDef)):
                assert (
                    function.end_lineno - function.lineno + 1 <= 40
                ), function.name


def main():
    """Run deterministic checks without device contact or account credentials."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    generator_checks()
    source_checks()
    logger.info(
        "SPOTIFY_AUTH_PAGE_STATIC=PASS deterministic assets private unavailable"
    )


if __name__ == "__main__":
    main()
