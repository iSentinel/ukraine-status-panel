#!/usr/bin/env python3
"""Run the complete HistoryStore with sanitizers and an original v1 NVS fixture."""
import argparse
import base64
import hashlib
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
LEGACY_SHA = "961c9922035341649e17a74f3765717ef59c0cfc21565c006e3c2e7951ee4e43"


def check_wiring():
    """Read group and subgroup together from the owning outage-client label."""
    text = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    assert re.search(
        r"id\(history_store\)\.record_schedule\(\s*"
        r"id\(outage_client\)\.group_label\(\),\s*"
        r"id\(outage_client\)\.today_outage_minutes\(\),\s*"
        r"id\(outage_client\)\.tomorrow_outage_minutes\(\)\);",
        text,
    ), "History must receive the complete group label"
    fixture = ROOT / "tests/fixtures/history-v1-schedule-group3.bin"
    if fixture.is_file():
        payload = fixture.read_bytes()
    else:
        archived = fixture.with_suffix(".bin.b64")
        payload = base64.b64decode(archived.read_text().strip(), validate=True)
    assert len(payload) == 404
    assert hashlib.sha256(payload).hexdigest() == LEGACY_SHA
    return payload


def compiler_command(binary):
    """Build the sanitizer command for the complete production module."""
    return [
        "g++",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        # The existing POD reset uses memset; its runtime remains sanitizer-checked.
        "-Wno-class-memaccess",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-fno-pie",
        "-no-pie",
        "-I",
        str(ROOT / "tests/history_host/shims"),
        "-I",
        str(ROOT / "components/usp_history"),
        "-I",
        str(ROOT / "include"),
        str(ROOT / "tests/history-subgroup-native.cpp"),
        str(ROOT / "components/usp_history/history_store.cpp"),
        "-o",
        str(binary),
    ]


def compile_fixture(directory):
    """Use unchanged production source; replace only framework storage and clock."""
    binary = directory / "history-subgroup"
    command = compiler_command(binary)
    with (directory / "compile.log").open("w") as log:
        result = subprocess.run(command, stdout=log, stderr=log, cwd=ROOT)
    if result.returncode:
        logger.error("{}", (directory / "compile.log").read_text()[-3000:])
        result.check_returncode()
    return binary


def execute(directory, fixture):
    """Preserve complete results and report meaningful behavioral coverage."""
    directory.mkdir(parents=True, exist_ok=False)
    binary = compile_fixture(directory)
    result = subprocess.run(
        [str(binary), str(fixture)],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=30,
    )
    (directory / "run.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        logger.error("{}", (directory / "run.log").read_text()[-3000:])
        result.check_returncode()
    logger.info("{}", result.stdout.strip())


def main():
    """Expose a saved-evidence option without performing any device operation."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    payload = check_wiring()
    with TemporaryDirectory(prefix="usp-history-subgroup-") as temporary:
        fixture = Path(temporary) / "legacy-v1.bin"
        fixture.write_bytes(payload)
        execute(args.build_dir or Path(temporary) / "result", fixture)


if __name__ == "__main__":
    main()
