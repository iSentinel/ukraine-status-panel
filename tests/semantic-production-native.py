#!/usr/bin/env python3
"""Execute production C++ contracts; optionally reject isolated mutations.

Use --mutation-probes --output artifacts/<new-directory> for durable evidence.
No panel connection, firmware image build, or OTA operation is performed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import resource
import shutil
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/semantic_production"
INPUTS = {
    "ota": ("components/usp_ota_guard",),
    "guard": ("components/usp_ota_guard",),
    "room": (
        "components/usp_room_history",
        "include/usp_room_chart.h",
        "include/usp_room_chart_model.h",
    ),
    "calendar": ("components/usp_outage/outage_calendar.h",),
    "sht4x": ("components/usp_optional_sht4x/optional_sht4x.cpp",),
}
MUTATIONS = (
    (
        "ota-offset-plus-one",
        "ota",
        "components/usp_ota_guard/ota_write_pipeline.cpp",
        "offset != this->written_bytes_",
        "offset != this->written_bytes_ + 1",
    ),
    (
        "guard-offset-plus-one",
        "guard",
        "components/usp_ota_guard/ota_guard.cpp",
        "if (offset != this->writer_bytes_seen_) {",
        "if (offset != this->writer_bytes_seen_ + 1) {",
    ),
    (
        "sht4x-clamp-removed",
        "sht4x",
        "components/usp_optional_sht4x/optional_sht4x.cpp",
        "std::clamp(humidity_raw, 0.0f, 100.0f)",
        "humidity_raw",
    ),
    (
        "sht4x-coefficient",
        "sht4x",
        "components/usp_optional_sht4x/optional_sht4x.cpp",
        "125.0f * float(data[1])",
        "12.5f * float(data[1])",
    ),
    (
        "room-break-times-ten",
        "room",
        "include/usp_room_chart.h",
        "BREAK_SECONDS = ROOM_HISTORY_BUCKET_SECONDS * 2U",
        "BREAK_SECONDS = ROOM_HISTORY_BUCKET_SECONDS * 20U",
    ),
    (
        "outage-day-shift",
        "calendar",
        "components/usp_outage/outage_calendar.h",
        "if (tomorrow) now = following_date(now);",
        "if (tomorrow) now = following_date(following_date(now));",
    ),
)


def put(path: Path, text: str) -> None:
    """Write test fixtures only inside the selected evidence directory."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def prepare(work: Path, kind: str) -> None:
    """Copy production inputs, keeping all changes outside production."""
    for relative in INPUTS[kind]:
        source, target = ROOT / relative, work / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if source.is_dir():
            shutil.copytree(
                source,
                target,
                ignore=shutil.ignore_patterns("__pycache__", "*.pyc"),
            )
        else:
            shutil.copy2(source, target)
    if kind == "ota":
        put(
            work / "shims/esp_err.h",
            "#pragma once\nusing esp_err_t=int;\n" "constexpr int ESP_OK=0;\n",
        )
        put(
            work / "shims/esp_ota_ops.h",
            "#pragma once\nusing esp_ota_handle_t=unsigned;\n",
        )
        put(
            work / "shims/esp_partition.h",
            "#pragma once\nstruct esp_partition_t {};\n",
        )
    if kind == "room":
        room_shims(work)


def room_shims(work: Path) -> None:
    """Replace only LCD calls, keeping the real room-history header."""
    target = (
        work / "shims/esphome/components/usp_room_history/room_history.h"
    )
    put(target, "#include <room_history.h>\n")
    put(
        work / "shims/esphome/components/lvgl/lvgl_esphome.h",
        "#pragma once\nstruct lv_point_precise_t {int x,y;};\n"
        "namespace esphome::lvgl {struct LvLineType {void *obj;};}\n"
        "constexpr int LV_OBJ_FLAG_HIDDEN=1;\n"
        "inline void lv_obj_add_flag(void *, int) {}\n"
        "inline void lv_obj_clear_flag(void *, int) {}\n"
        "inline void lv_line_set_points(void *,lv_point_precise_t *,"
        "int) {}\n",
    )


def production_method(source: str, name: str) -> str:
    """Extract one complete C++ method without rewriting its statements."""
    position = source.index("OtaGuard::" + name + "(")
    start = source.rfind("\n", 0, position) + 1
    opened = source.index("{", position)
    end, depth = opened + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def harness(work: Path, kind: str) -> None:
    """Bind fixture assertions to copied production expressions or methods."""
    text = (FIXTURES / f"{kind}.cpp").read_text()
    if kind == "guard":
        source = (work / "components/usp_ota_guard/ota_guard.cpp").read_text()
        method = production_method(source, "add_writer_dry_run_chunk")
        put(work / "executed-production-method.cpp", method)
        text = text.replace("// PRODUCTION_METHOD", method)
    if kind == "sht4x":
        source = (work / INPUTS[kind][0]).read_text()
        lines = []
        for name in ("RAW_MAX", "humidity_raw", "humidity"):
            declaration = "static constexpr" if name == "RAW_MAX" else "const"
            pattern = rf"^\s*{declaration} float {name}\s*=\s*(.+?);\s*$"
            match = re.search(pattern, source, re.MULTILINE)
            if match is None:
                raise ValueError(f"Missing production expression: {name}")
            lines.append(f"const float {name} = {match.group(1)};")
        text = text.replace("// PRODUCTION_EXPRESSIONS", "\n".join(lines))
    put(work / "test.cpp", text)


def compile_command(work: Path, kind: str) -> list[str]:
    """Compile host executables with sanitizers and assertions enabled."""
    command = [
        "g++",
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-g",
        "-O1",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-fno-pie",
        "-no-pie",
        "-pthread",
        "-I",
        str(work / "shims"),
        "-I",
        str(work / "include"),
        "-I",
        str(ROOT / "tests/room_history_host/shims"),
    ]
    for component in ("usp_ota_guard", "usp_room_history", "usp_outage"):
        command += ["-I", str(work / "components" / component)]
    command.append(str(work / "test.cpp"))
    if kind == "ota":
        command.append(
            str(work / "components/usp_ota_guard/ota_write_pipeline.cpp")
        )
    if kind == "room":
        command.append(
            str(work / "components/usp_room_history/room_history.cpp")
        )
    return command + ["-o", str(work / "test")]


def execute(work: Path, kind: str, mutation=None) -> dict:
    """Distinguish compile errors from executable assertion rejection."""
    prepare(work, kind)
    if mutation is not None:
        _, _, relative, before, after = mutation
        path = work / relative
        text = path.read_text()
        if text.count(before) != 1:
            raise ValueError(f"Mutation anchor must be unique: {relative}")
        path.write_text(text.replace(before, after))
    harness(work, kind)
    command = compile_command(work, kind)
    put(work / "compile-command.json", json.dumps(command, indent=2))
    build = subprocess.run(command, capture_output=True, text=True, timeout=60)
    put(work / "compile.log", build.stdout + build.stderr)
    if build.returncode:
        raise RuntimeError(f"Compilation failed, not a mutation kill: {work}")
    result = subprocess.run(
        [str(work / "test")], capture_output=True, text=True, timeout=20
    )
    output = result.stdout + result.stderr
    put(work / "run.log", output)
    if mutation is None:
        passed = result.returncode == 0 and "PRODUCTION=PASS" in output
    else:
        passed = result.returncode != 0 and "Assertion" in output
    if not passed:
        raise AssertionError(f"Unexpected executable result: {work}: {output}")
    logger.info("{}: {}", work.name, "rejected" if mutation else "PASS")
    return {
        "case": work.name,
        "kind": kind,
        "compile_exit": build.returncode,
        "run_exit": result.returncode,
        "mutation_rejected": bool(mutation),
        "status": "PASS",
    }


def source_hashes() -> dict[str, str]:
    """Record every relevant production input for before/after comparison."""
    result = {}
    for paths in INPUTS.values():
        for relative in paths:
            path = ROOT / relative
            files = path.rglob("*") if path.is_dir() else [path]
            for file in files:
                if file.is_file() and file.suffix in {".cpp", ".h", ".py"}:
                    result[str(file.relative_to(ROOT))] = hashlib.sha256(
                        file.read_bytes()
                    ).hexdigest()
    return result


def run(output: Path, mutations: bool) -> None:
    """Require all baseline executables and selected mutation kills."""
    output.mkdir(parents=True, exist_ok=False)
    before = source_hashes()
    rows = [execute(output / kind, kind) for kind in INPUTS]
    if mutations:
        rows += [
            execute(output / item[0], item[1], item) for item in MUTATIONS
        ]
    after = source_hashes()
    if before != after:
        raise AssertionError(
            "Production inputs changed during coverage checks"
        )
    put(
        output / "report.json",
        json.dumps(
            {
                "status": "PASS",
                "production_unchanged": True,
                "source_hashes": before,
                "results": rows,
                "device_contact": False,
                "firmware_builds": 0,
                "ota_attempts": 0,
                "scope": "Four semantic families; six explicit variants. "
                "Not a claim of coverage of every OTA module.",
            },
            indent=2,
        )
        + "\n",
    )


def main() -> None:
    """Offer durable artifact probes and an ordinary temporary regression."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-probes", action="store_true")
    parser.add_argument(
        "--output", type=Path, help="New artifacts subdirectory"
    )
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if args.mutation_probes and args.output is None:
        parser.error("--mutation-probes requires --output under artifacts")
    if args.output is not None:
        output = args.output.resolve()
        if not output.is_relative_to((ROOT / "artifacts").resolve()):
            parser.error("--output must be inside project artifacts")
        run(output, args.mutation_probes)
    else:
        with tempfile.TemporaryDirectory(prefix="usp-semantic-") as name:
            run(Path(name) / "baseline", False)
    logger.info("SEMANTIC_PRODUCTION_NATIVE=PASS")


if __name__ == "__main__":
    main()
