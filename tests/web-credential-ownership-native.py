#!/usr/bin/env python3
"""Execute the actual Web base setters and middleware under controlled threads."""

import argparse
import subprocess
import tempfile
from pathlib import Path

from loguru import logger
from firmware_version_guard import CURRENT_FIRMWARE_MARKER

ROOT = Path(__file__).resolve().parents[1]


def run_native():
    """Compile actual component code; only ESPHome/HTTP platform boundaries are doubles."""
    fixtures = ROOT / "tests/web_credentials_host"
    with tempfile.TemporaryDirectory(
        prefix="usp-owned-credentials-"
    ) as directory:
        folder = Path(directory)
        binary = folder / "credentials"
        subprocess.run(
            [
                "g++",
                "-std=c++17",
                "-g",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-pthread",
                "-I",
                str(fixtures),
                "-I",
                str(ROOT / "components/web_server_base"),
                str(fixtures / "main.cpp"),
                str(ROOT / "components/web_server_base/web_server_base.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        subprocess.run([str(binary)], check=True, timeout=30)


def check_integration():
    """Require the owned store and per-request snapshot in the private override."""
    directory = ROOT / "components/web_server_base"
    header = (directory / "web_server_base.h").read_text()
    owned = (directory / "owned_credentials.h").read_text()
    assert "using Credentials = OwnedCredentials;" in header
    assert "this->credentials_->snapshot()" in header
    assert (
        "credentials.username_value(), credentials.password_value()" in header
    )
    assert "credentials_.set_password(auth_password)" in header
    assert "mutable std::mutex mutex_" in owned
    assert "return this->value_;" in owned
    yaml = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    assert "      - web_server_base\n" in yaml
    assert "usp_qa_reset_web_password_136" not in yaml
    assert CURRENT_FIRMWARE_MARKER in yaml


def main():
    """Run offline ownership/concurrency checks without touching device credentials."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    check_integration()
    run_native()
    logger.info(
        "WEB_CREDENTIAL_OWNERSHIP=PASS actual setters/middleware/store "
        "caller-reallocation held-snapshot nonblocking-auth parallel10000 "
        "reject-body-upload ASAN UBSAN; HTTP/platform doubles; no TSAN"
    )


if __name__ == "__main__":
    main()
