#!/usr/bin/env python3
"""Execute proposed map color consumer fragments against preserved baselines."""
import argparse
import resource
import time
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/map-color-oracles.cpp.in"
YAML_CALL = re.compile(
    r"(?m)^([ ]*)uint32_t color = usp_alert_region_colors::color_hex\(\n"
    r"\1    state, palette\);\n"
)


def fragments(config, header_path):
    """Extract the actual proposed header wrapper and both YAML expressions."""
    header = header_path.read_text()
    start = header.index("inline uint32_t region_color_hex(")
    end = header.index("\n}\n", start) + 3
    function = header[start:end]
    assert (
        function.count(
            "return usp_alert_region_colors::color_hex(state, palette);"
        )
        == 1
    )
    result = [function.replace("region_color_hex", "current_full_color", 1)]
    text = config.read_text()
    matches = list(YAML_CALL.finditer(text))
    assert len(matches) == 2, len(matches)
    for index, match in enumerate(matches):
        import textwrap

        body = textwrap.dedent(match.group(0))
        result.append(
            f"uint32_t current_yaml_color{index}(uint8_t state, uint8_t palette) {{\n"
            + body
            + "return color;\n}\n"
        )
    return result


def fixture(parts):
    """Add actual consumer execution to the preserved historical oracle."""
    text = FIXTURE.read_text()
    current = "\n".join(parts)
    text = text.replace("int main() {", current + "\nint main() {", 1)
    extra = """
      assert(current_full_color(state, palette) == actual);
      assert(current_yaml_color0(static_cast<uint8_t>(state), palette) == actual);
      assert(current_yaml_color1(static_cast<uint8_t>(state), palette) == actual);
"""
    text = text.replace(
        "      comparisons += 3;", extra + "      comparisons += 6;", 1
    )
    assert "comparisons += 6;" in text
    return text


def compiler_command(include, source, binary):
    """Use the same sanitizer options for baseline and valid variants."""
    return [
        "g++",
        "-std=c++17",
        "-O1",
        "-g",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-no-pie",
        "-I",
        str(include),
        str(source),
        "-o",
        str(binary),
    ]


def run_case(output, name, text):
    """Compile valid consumer variants before executing their assertions."""
    case = output / name
    case.mkdir(exist_ok=False)
    source = case / "probe.cpp"
    source.write_text(text)
    include = case / "include"
    include.mkdir()
    for filename in (
        "usp_alert_region_colors.h",
        "usp_alert_palette_0_115_114.h",
    ):
        shutil.copy2(ROOT / "include" / filename, include)
    binary = case / "probe"
    command = compiler_command(include, source, binary)
    with (case / "compile.log").open("x") as log:
        compiled = subprocess.run(
            command,
            stdout=log,
            stderr=subprocess.STDOUT,
            cwd=case,
            timeout=120,
        )
    assert compiled.returncode == 0, name
    with (case / "run.log").open("x") as log:
        ran = subprocess.run(
            [str(binary)],
            stdout=log,
            stderr=subprocess.STDOUT,
            cwd=case,
            timeout=60,
        )
    return dict(name=name, compile_exit=0, run_exit=ran.returncode)


def variants(parts):
    """Change each actual consumer once without causing a compile failure."""
    changes = []
    for index, part in enumerate(parts):
        if index == 0:
            changed = part.replace(
                "color_hex(state, palette)", "color_hex(state + 1, palette)", 1
            )
        else:
            changed = part.replace(
                "state, palette);", "state, palette ^ 1U);", 1
            )
        assert changed != part
        updated = list(parts)
        updated[index] = changed
        changes.append((f"consumer{index}", fixture(updated)))
    return changes


def arguments():
    """Select real inputs and a fresh artifact output folder."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT
        / "artifacts"
        / ("map-color-native-" + str(time.time_ns())),
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=ROOT / "firmware/ukraine-status-panel.yaml",
    )
    parser.add_argument(
        "--header",
        type=Path,
        default=ROOT / "include/usp_fullscreen_map_cache.h",
    )
    return parser.parse_args()


def main():
    """Keep every probe in a new artifact folder without device access."""
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    args = arguments()
    args.output.mkdir(parents=True, exist_ok=False)
    parts = fragments(args.config, args.header)
    baseline = run_case(args.output, "baseline", fixture(parts))
    assert baseline["run_exit"] == 0
    assert (
        "MAP_COLOR_COMPARISONS=3145728"
        in (args.output / "baseline/run.log").read_text()
    )
    negatives = [
        run_case(args.output, name, text) for name, text in variants(parts)
    ]
    assert all(item["run_exit"] != 0 for item in negatives)
    report = dict(
        status="PASS",
        actual_consumer_fragments=3,
        comparisons=3145728,
        negative_controls=negatives,
        baseline=baseline,
        fragment_sha256=[
            hashlib.sha256(x.encode()).hexdigest() for x in parts
        ],
        production_modified=False,
        device_contact=False,
        firmware_image_built=False,
    )
    (args.output / "report.json").write_text(
        json.dumps(report, indent=2) + "\n"
    )
    logger.info(
        "MAP_COLOR_ACTUAL_CONSUMERS=PASS comparisons3145728 negatives3"
    )


if __name__ == "__main__":
    main()
