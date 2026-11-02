#!/usr/bin/env python3
"""Write hashed CI locks from reviewed pip --dry-run --report files.

Run with Python 3.12 on Linux x86_64. Reports should resolve ESPHome together
with requirements-dev.txt, and separately pip/setuptools/wheel/packaging.
No packages are installed and no network requests are made by this script.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

from loguru import logger

HEADER = (
    "# CT106 resolution for Linux x86_64 / CPython 3.12.\n"
    "# Exact versions and source/wheel hashes; see docs/CI_DEPENDENCIES.md.\n"
)
BUILD_TOOLS = {"pip", "setuptools", "wheel", "packaging"}


def records(path: Path) -> dict[str, tuple[str, str]]:
    """Keep exact versions and archive hashes for the target platform."""
    report = json.loads(path.read_text())
    environment = report["environment"]
    if environment["python_version"] != "3.12":
        raise ValueError("CI locks require a Python 3.12 report")
    if environment["sys_platform"] != "linux":
        raise ValueError("CI locks require a Linux report")
    if environment["platform_machine"] not in ("x86_64", "AMD64"):
        raise ValueError("CI locks require x86_64")
    result = {}
    for item in report["install"]:
        metadata = item["metadata"]
        name = metadata["name"].lower().replace("_", "-")
        version = metadata["version"]
        digest = item["download_info"]["archive_info"]["hashes"]["sha256"]
        if not re.fullmatch(r"[a-z0-9_.-]+", name):
            raise ValueError("Invalid package name")
        if not re.fullmatch(r"[A-Za-z0-9_.!+~-]+", version):
            raise ValueError("Invalid exact package version")
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError("Invalid archive SHA256")
        if name in result:
            raise ValueError(f"Duplicate report package: {name}")
        result[name] = version, digest
    if not result:
        raise ValueError("Empty report")
    return result


def render(rows: dict) -> str:
    """Produce stable lock text without copying source URLs or credentials."""
    lines = [
        f"{name}=={version} --hash=sha256:{digest}"
        for name, (version, digest) in sorted(rows.items())
    ]
    return HEADER + "\n".join(lines) + "\n"


def locks(runtime: Path, build: Path) -> dict[str, str]:
    """Combine runtime packages with the exact tools used for source builds."""
    main = records(runtime)
    tools = {
        name: value
        for name, value in records(build).items()
        if name in BUILD_TOOLS
    }
    if tools.keys() != BUILD_TOOLS:
        raise ValueError("Bootstrap report is missing a required build tool")
    for name, value in tools.items():
        if name in main and main[name] != value:
            raise ValueError(f"Runtime/bootstrap conflict: {name}")
        main[name] = value
    return {
        "requirements-ci.lock": render(main),
        "requirements-ci-bootstrap.lock": render(tools),
    }


def main() -> None:
    """Write both locks, or compare them without changing any file."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-report", type=Path, required=True)
    parser.add_argument("--bootstrap-report", type=Path, required=True)
    parser.add_argument(
        "--output-dir", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    files = locks(args.runtime_report, args.bootstrap_report)
    for name, text in files.items():
        path = args.output_dir / name
        if args.check:
            if path.read_text() != text:
                raise SystemExit(f"Lock differs from reviewed reports: {name}")
        else:
            path.write_text(text)
    logger.info("CI_LOCK_GENERATION=PASS files=2 check={}", args.check)


if __name__ == "__main__":
    main()
