"""Check actual RoomHistory time ordering and two compiled negative controls."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components/usp_room_history"
FIXTURE = ROOT / "tests/room-history-time-order-native.cpp"
GUARD = "if (epoch < this->samples_[latest].epoch) return false;"


def digest(path):
    """Hash one test or production input."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build_run(component, output, name, expected):
    """A failed compilation never counts as a detected behavioral defect."""
    binary = output / (name + ".bin")
    command = [
        "g++",
        "-std=c++20",
        "-O1",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined,float-cast-overflow",
        "-fno-omit-frame-pointer",
        "-fno-pie",
        "-no-pie",
        "-g",
        "-I",
        str(ROOT / "tests/room_history_host/shims"),
        "-I",
        str(component),
        str(FIXTURE),
        str(component / "room_history.cpp"),
        "-o",
        str(binary),
    ]
    built = subprocess.run(command, capture_output=True, text=True)
    (output / (name + "-compile.log")).write_text(built.stdout + built.stderr)
    assert built.returncode == 0, name
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    (output / (name + "-run.log")).write_text(result.stdout + result.stderr)
    assert result.returncode == expected, (name, result.returncode)
    return dict(name=name, compiled=True, observed_exit=result.returncode)


def negatives(output, source):
    """Both incorrect policies must compile and fail for their specific reason."""
    assert source.count(GUARD) == 1
    results = []
    for name, replacement, expected in (
        ("missing-guard", "", 31),
        ("reject-equal", GUARD.replace("epoch <", "epoch <="), 32),
    ):
        directory = output / name
        directory.mkdir()
        shutil.copy2(
            COMPONENT / "room_history.h", directory / "room_history.h"
        )
        (directory / "room_history.cpp").write_text(
            source.replace(GUARD, replacement)
        )
        results.append(build_run(directory, output, name, expected))
    return results


def run_checks(output):
    """Bind results to production bytes and leave canonical inputs unchanged."""
    output.mkdir()
    paths = (
        COMPONENT / "room_history.cpp",
        COMPONENT / "room_history.h",
        FIXTURE,
    )
    before = {str(p.relative_to(ROOT)): digest(p) for p in paths}
    source = (COMPONENT / "room_history.cpp").read_text()
    positive = build_run(COMPONENT, output, "positive", 0)
    mutations = negatives(output, source)
    assert before == {str(p.relative_to(ROOT)): digest(p) for p in paths}
    report = dict(
        status="PASS",
        positive=positive,
        negatives=mutations,
        source_inputs=before,
        dense_time_cases=3000,
        ring_wrap_checked=True,
        actual_production_component=True,
        production_modified=False,
        physical_sensor_or_clock_adjustment_tested=False,
    )
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    logger.info(
        "ROOM_HISTORY_TIME_ORDER_NATIVE=PASS actual_component cases3000 ring_wrap negatives2"
    )


def main():
    """Keep optional evidence in a new directory; ordinary CI uses temporary data."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        run_checks(args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(
            prefix="usp-room-order-"
        ) as temporary:
            run_checks(Path(temporary) / "evidence")


if __name__ == "__main__":
    main()
