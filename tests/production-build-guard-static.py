#!/usr/bin/env python3
"""Validate the exclusive production build wrapper without compiling firmware."""

from pathlib import Path
import fcntl
import shutil
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WRAPPER = ROOT / "scripts" / "compile-production.sh"
DOC = ROOT / "docs" / "BUILD.md"
LOCK = ROOT / ".work-state" / "production-build.lock"


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(WRAPPER), *args],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    text = WRAPPER.read_text()
    needles = (
        "flock -n 9",
        "source_fingerprint",
        "ROOT/components",
        "ROOT/include",
        "ROOT/assets",
        "ROOT/firmware/webui",
        "__pycache__",
        "*.pyc",
        "*.pyo",
        "/opt/esphome-2026.10.1/bin/esphome",
        'EXPECTED_ESPHOME_VERSION="2026.10.1"',
    )
    for needle in needles:
        if needle not in text:
            raise AssertionError(f"missing build guard: {needle}")
    if "./scripts/compile-production.sh" not in DOC.read_text():
        raise AssertionError("BUILD.md bypasses production compile wrapper")

    help_result = run("--help")
    if help_result.returncode != 0 or "Usage:" not in help_result.stdout:
        raise AssertionError("--help contract failed")

    bad_result = run("--invalid-option")
    if bad_result.returncode != 2:
        raise AssertionError(f"unknown option rc={bad_result.returncode}, expected 2")

    fp_result = run("--fingerprint")
    fingerprint = fp_result.stdout.strip()
    if fp_result.returncode != 0 or not re.fullmatch(r"[0-9a-f]{64}", fingerprint):
        raise AssertionError("fingerprint mode did not return one SHA256")

    probe = ROOT / "components" / ".production-build-guard-probe"
    try:
        cache = probe / "__pycache__"
        cache.mkdir(parents=True, exist_ok=True)
        (cache / "ignored.cpython-312.pyc").write_bytes(b"generated-bytecode")
        ignored = run("--fingerprint")
        if ignored.returncode != 0 or ignored.stdout.strip() != fingerprint:
            raise AssertionError("generated Python cache changes source fingerprint")

        source = probe / "source-contract.txt"
        source.write_text("real production input\n")
        changed = run("--fingerprint")
        if changed.returncode != 0 or changed.stdout.strip() == fingerprint:
            raise AssertionError("real component source does not change fingerprint")
    finally:
        shutil.rmtree(probe, ignore_errors=True)

    restored = run("--fingerprint")
    if restored.returncode != 0 or restored.stdout.strip() != fingerprint:
        raise AssertionError("fingerprint did not restore after probe cleanup")

    LOCK.parent.mkdir(parents=True, exist_ok=True)
    with LOCK.open("w") as handle:
        fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        locked = run("--fingerprint")
    if locked.returncode != 75 or "already running" not in locked.stderr:
        raise AssertionError("concurrent build lock is not enforced")

    print("PRODUCTION_BUILD_GUARD_STATIC=PASS " f"fingerprint={fingerprint[:12]}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"PRODUCTION_BUILD_GUARD_STATIC=FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
