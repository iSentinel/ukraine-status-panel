#!/usr/bin/env python3
"""Execute production alarm scheduling on timezone-derived Europe/Kyiv traces.

Use --output artifacts/<new-directory> to retain generated UTC/local fixtures.
Add --mutation-probes to require rejection of two isolated header mutations.
No clock, alarm setting, sound output, firmware image, or device is changed.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import resource
import subprocess
import tempfile
from zoneinfo import ZoneInfo

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "components/usp_alarm/alarm_schedule.h"
ZONE = ZoneInfo("Europe/Kyiv")
TIMES = (180, 210, 239)


def transitions(year: int) -> list[datetime]:
    """Find offset changes in UTC; do not hardcode transition dates."""
    start = datetime(year, 1, 1, tzinfo=timezone.utc)
    end = datetime(year + 1, 1, 1, tzinfo=timezone.utc)
    result = []
    current = start
    while current < end:
        following = current + timedelta(hours=1)
        if current.astimezone(ZONE).utcoffset() != (
            following.astimezone(ZONE).utcoffset()
        ):
            minute = current
            while minute.astimezone(ZONE).utcoffset() == (
                current.astimezone(ZONE).utcoffset()
            ):
                minute += timedelta(minutes=1)
            result.append(minute)
        current = following
    if len(result) != 2:
        raise AssertionError(f"Expected two recorded transitions in {year}")
    return result


def frames(center: datetime) -> list[dict]:
    """Choose the earliest real instant at or after each civil alarm time."""
    result = []
    fired = set()
    for offset in range(-80, 81):
        utc = center + timedelta(minutes=offset)
        local = utc.astimezone(ZONE)
        minute = local.hour * 60 + local.minute
        due = 0
        for index, target in enumerate(TIMES):
            if index not in fired and minute >= target:
                fired.add(index)
                due |= 1 << index
        result.append(
            {
                "utc": int(utc.timestamp()),
                "local": local.isoformat(),
                "calendar": [
                    local.year,
                    local.month,
                    local.day,
                    local.hour,
                    local.minute,
                    (local.weekday() + 1) % 7 + 1,
                ],
                "due": due,
            }
        )
    if result[0]["due"] or sum(row["due"].bit_count() for row in result) != 3:
        raise AssertionError("Trace does not enclose all three first firings")
    return result


def generated_cases(traces: list[list[dict]]) -> str:
    """Serialize the independent timezone oracle as C++ input fixtures."""
    blocks = ["static constexpr unsigned TIMES[]{180,210,239};"]
    descriptors = []
    for index, trace in enumerate(traces):
        rows = []
        for row in trace:
            civil = ",".join(map(str, row["calendar"])) + ",true"
            rows.append(f"  {{{{{{{civil}}},{row['utc']}U}},{row['due']}}},")
        blocks.append(
            f"static const Frame TRACE{index}[] = {{\n"
            + "\n".join(rows)
            + "\n};"
        )
        spring = sum(row["due"] == 7 for row in trace) == 1
        descriptors.append(
            f"{{TRACE{index},161,{'true' if spring else 'false'}}}"
        )
    blocks.append("static const Case CASES[]{" + ",".join(descriptors) + "};")
    return "\n".join(blocks)


def compile_command(directory: Path) -> list[str]:
    """Build a server executable with assertions and sanitizers."""
    return [
        "g++",
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-g",
        "-fsanitize=address,undefined",
        "-fno-pie",
        "-no-pie",
        "-I",
        str(directory),
        "-I",
        str(ROOT / "include"),
        str(directory / "test.cpp"),
        "-o",
        str(directory / "test"),
    ]


def execute(work: Path, source: str, name: str, mutation=None) -> dict:
    """Require compilation before accepting an executable assertion failure."""
    directory = work / name
    directory.mkdir()
    header = HEADER.read_text()
    if mutation:
        before, after = mutation
        if header.count(before) != 1:
            raise ValueError("Expected one production mutation anchor")
        header = header.replace(before, after)
    (directory / "alarm_schedule.h").write_text(header)
    (directory / "test.cpp").write_text(source)
    command = compile_command(directory)
    compiled = subprocess.run(
        command, capture_output=True, text=True, timeout=60
    )
    (directory / "compile.log").write_text(compiled.stdout + compiled.stderr)
    if compiled.returncode:
        raise RuntimeError(f"Compile failure is not a mutation kill: {name}")
    result = subprocess.run(
        [str(directory / "test")], capture_output=True, text=True, timeout=20
    )
    check_execution(directory, name, result, bool(mutation))
    return {
        "case": name,
        "compile_exit": compiled.returncode,
        "run_exit": result.returncode,
        "mutation_rejected": bool(mutation),
    }


def check_execution(directory, name, result, mutated) -> None:
    """Separate valid assertion rejection from compiler or runtime failures."""
    text = result.stdout + result.stderr
    (directory / "run.log").write_text(text)
    valid = (
        (result.returncode != 0 and "Assertion" in text)
        if mutated
        else (
            result.returncode == 0
            and "ALARM_KYIV_TIMEZONE_NATIVE=PASS" in text
        )
    )
    if not valid:
        raise AssertionError(f"Unexpected runtime result: {name}: {text}")
    logger.info("{}: PASS exit={}", name, result.returncode)


def run(work: Path, mutations: bool) -> None:
    """Check four years of spring and fall using the unchanged header."""
    work.mkdir(parents=True, exist_ok=False)
    digest = hashlib.sha256(HEADER.read_bytes()).hexdigest()
    centers = [
        value for year in range(2023, 2027) for value in transitions(year)
    ]
    traces = [frames(center) for center in centers]
    (work / "timezone-traces.json").write_text(json.dumps(traces, indent=2))
    fixture = (ROOT / "tests/alarm-kyiv-timezone-native.cpp.in").read_text()
    source = fixture.replace("// GENERATED_CASES", generated_cases(traces))
    results = [execute(work, source, "baseline")]
    if mutations:
        cases = {
            "spring-gap-disabled": (
                "now == before + 61U",
                "now == before + 62U",
            ),
            "fall-dedup-disabled": (
                "if (fired_day == today) return true;",
                "if (fired_day == today) return false;",
            ),
        }
        results += [
            execute(work, source, name, mutation)
            for name, mutation in cases.items()
        ]
    if hashlib.sha256(HEADER.read_bytes()).hexdigest() != digest:
        raise AssertionError("Production schedule header changed")
    save_report(work, digest, centers, results)


def save_report(work, digest, centers, results) -> None:
    """Retain the timezone snapshot and exact production input identity."""
    metadata = Path("/usr/share/zoneinfo/tzdata.zi")
    report = {
        "status": "PASS",
        "timezone": ZONE.key,
        "years": [2023, 2024, 2025, 2026],
        "tzdata": (
            metadata.read_text().splitlines()[0]
            if metadata.exists()
            else "system ZoneInfo; version metadata unavailable"
        ),
        "zone_sha256": hashlib.sha256(
            Path("/usr/share/zoneinfo/Europe/Kyiv").read_bytes()
        ).hexdigest(),
        "transitions": [value.isoformat() for value in centers],
        "results": results,
        "production_header_sha256": digest,
        "production_unchanged": True,
        "device_contact": False,
    }
    (work / "report.json").write_text(json.dumps(report, indent=2) + "\n")


def main() -> None:
    """Expose durable artifact probes and a temporary ordinary regression."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--mutation-probes", action="store_true")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if args.mutation_probes and args.output is None:
        parser.error("--mutation-probes requires --output under artifacts")
    if args.output:
        output = args.output.resolve()
        if not output.is_relative_to((ROOT / "artifacts").resolve()):
            parser.error("--output must be inside project artifacts")
        run(output, args.mutation_probes)
    else:
        with tempfile.TemporaryDirectory(prefix="usp-kyiv-dst-") as directory:
            run(Path(directory) / "check", False)


if __name__ == "__main__":
    main()
