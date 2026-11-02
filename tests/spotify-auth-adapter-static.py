#!/usr/bin/env python3
"""Check private-only adapter admission and the actual auth configuration validator."""
import argparse
import ast
from pathlib import Path
from types import SimpleNamespace
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def check_validator():
    """Run the real validator against absent, empty and configured credentials."""
    path = ROOT / "components/usp_spotify_auth/__init__.py"
    tree = ast.parse(path.read_text())
    fn = next(
        n
        for n in tree.body
        if isinstance(n, ast.FunctionDef)
        and n.name == "validate_authenticated"
    )
    module = ast.Module(body=[fn], type_ignores=[])
    ast.fix_missing_locations(module)
    rows = (
        {},
        {"web_server": {}},
        {"web_server": {"auth": {}}},
        {"web_server": {"auth": {"username": "admin", "password": ""}}},
        {"web_server": {"auth": {"username": "", "password": "fixture"}}},
        {"web_server": {"auth": {"username": "admin", "password": "fixture"}}},
    )
    accepted = 0
    for full in rows:
        scope = {
            "cv": SimpleNamespace(Invalid=ValueError),
            "fv": SimpleNamespace(
                full_config=SimpleNamespace(get=lambda: full)
            ),
        }
        exec(compile(module, str(path), "exec"), scope)
        try:
            marker = {"id": "auth"}
            assert scope["validate_authenticated"](marker) is marker
            accepted += 1
        except ValueError:
            pass
    assert accepted == 1


def main():
    """Guard registration, bounded storage and target/public profile separation."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    check_validator()
    source = (
        ROOT / "components/usp_spotify_auth/spotify_auth.cpp"
    ).read_text()
    header = (ROOT / "components/usp_spotify_auth/spotify_auth.h").read_text()
    private = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    public = (
        ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml"
    ).read_text()
    assert "usp_spotify_auth:\n  id: spotify_auth_api" in private
    assert "application_client_id: !secret spotify_client_id" in private
    assert "application_redirect_uri: \"https://spotify-callback.example.invalid/\"" in private
    assert (
        "usp_spotify_auth:\n" not in public
        and "- usp_spotify_auth\n" not in public
    )
    assert "usp_spotify_auth_form.h" not in public
    assert (
        "add_handler(this)" in source
        and "add_handler_without_auth" not in source
    )
    assert "#ifdef USE_WEBSERVER_AUTH" in source
    assert "ALLOC_EXTERNAL" in source and "std::try_to_lock" in source
    assert "std::atomic<bool> registered_" in header
    assert "sizeof(SpotifyAuthApi) <= 4096" in header
    assert "esp_http_client" not in source and "preferences" not in source
    assert "query_not_allowed" in source and "no-store" in source
    assert "application_matches_(form)" in source
    assert "set_application_client_id" in header
    assert "application_client_id" not in public and "spotify_client_id" not in public
    page = (ROOT / "firmware/webui/spotify-auth/page.js").read_text()
    assert "data.application" in page and "applyApplication" in page
    assert "spotify-callback.example.invalid" not in page
    logger.info(
        "SPOTIFY_AUTH_ADAPTER_STATIC=PASS validator6 private_only auth_middleware PSRAM trylock"
    )


if __name__ == "__main__":
    main()
