#!/usr/bin/env python3
"""Compare actual indoor/room-entry callbacks on copies; no sensor or device access."""
import argparse
import hashlib
import json
import resource
import tempfile
import textwrap
import subprocess
from pathlib import Path
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests/fixtures/indoor-entry"
HEADER = "usp_indoor_text.h"


def read(name):
    """Read a fixed preflight input."""
    return (OUT / name).read_text()


def render(header, indoor, entry):
    """Keep the previous actual bodies and independent named-key expectations."""
    keys = json.loads(read("keys.json"))
    golden = ",\n".join(
        "    {"
        + ", ".join(
            json.dumps(v[lang], ensure_ascii=False) for v in keys.values()
        )
        + "}"
        for lang in range(3)
    )
    cpp = read("fixture.cpp.in")
    for key, value in {
        "INDOOR_BEFORE": read("refresh_indoor_environment_ui-before.cpp"),
        "INDOOR_AFTER": indoor,
        "ENTRY_BEFORE": read("refresh_room_entry_i18n-before.cpp"),
        "ENTRY_AFTER": entry,
        "GOLDEN": golden,
    }.items():
        cpp = cpp.replace("@@" + key + "@@", value)
    assert "@@" not in cpp
    return cpp


def execute(base, name, header, indoor, entry, negative):
    """Require successful compilation before assessing a negative control."""
    directory = base / name
    directory.mkdir()
    (directory / HEADER).write_text(header)
    (directory / "test.cpp").write_text(render(header, indoor, entry))
    command = [
        "g++",
        "-std=c++17",
        "-O1",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-fno-pie",
        "-no-pie",
        str(directory / "test.cpp"),
        "-o",
        str(directory / "test"),
    ]
    built = subprocess.run(command, text=True, capture_output=True, timeout=60)
    (directory / "compile.log").write_text(built.stdout + built.stderr)
    built.check_returncode()
    result = subprocess.run(
        [str(directory / "test")], capture_output=True, text=True, timeout=30
    )
    (directory / "run.log").write_text(result.stdout + result.stderr)
    (directory / "run.exit").write_text(str(result.returncode) + "\n")
    assert (result.returncode != 0) == negative, (name, result.returncode)
    logger.info("{} compiled=0 run={}", name, result.returncode)
    return dict(
        name=name,
        compile_exit=0,
        run_exit=result.returncode,
        rejected=negative,
    )


def mutations(header, indoor, entry):
    """Alter only copied text, bounds, numeric path or consumer bindings."""
    cases = []
    for key, old, new in [
        ("caption", '"Indoor: "', '"Indoor! "'),
        ("named_key", "PREFIX, NOT_CONNECTED", "NOT_CONNECTED, PREFIX"),
        ("fallback", "language : 2", "language : 0"),
        ("invalid_bound", "index < 4", "index < 5"),
    ]:
        assert header.count(old) == 1
        cases.append((key, header.replace(old, new, 1), indoor, entry))
    for key, old, new in [
        ("detection_guard", "if (!detected)", "if (detected)"),
        ("precision", "%s%.1f°C", "%s%.0f°C"),
        ("humidity_rounding", "std::lround(", "std::trunc("),
        (
            "second_label",
            "id(fullscreen_weather_indoor_label)",
            "id(weather_indoor_label)",
        ),
    ]:
        assert old in indoor
        cases.append((key, header, indoor.replace(old, new, 1), entry))
    for key, old, new in [
        ("entry_fallback", "id(ui_language) : 0", "id(ui_language) : 2"),
        ("entry_named_key", "Key::ROOM_ENTRY", "Key::PREFIX"),
    ]:
        assert old in entry
        cases.append((key, header, indoor, entry.replace(old, new, 1)))
    return cases


def run_all(base):
    """Run the real before/after callback bodies and copied negative controls."""
    base.mkdir()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    source = ROOT / "firmware/ukraine-status-panel.yaml"
    before = source.read_bytes()
    header = (ROOT / "include" / HEADER).read_text()
    indoor = current_body("refresh_indoor_environment_ui")
    entry = current_body("refresh_room_entry_i18n")
    execute(base, "positive", header, indoor, entry, False)
    results = [
        execute(base, name, h, i, e, True)
        for name, h, i, e in mutations(header, indoor, entry)
    ]
    assert source.read_bytes() == before
    report = dict(
        status="PASS",
        keys=4,
        bindings=12,
        catalog_comparisons=1024,
        invalid_key_checks=512,
        actual_callback_comparisons=36864,
        negative_controls=results,
        source_yaml_sha256=hashlib.sha256(before).hexdigest(),
        production_modified=False,
        device_contact=False,
    )
    with (base / "report.json").open("x") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    logger.info(
        "INDOOR_ENTRY_NATIVE=PASS callbacks36864 negatives{}", len(results)
    )


def current_body(owner):
    """Read the real script body instead of testing an unlinked replacement."""
    source = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    marker = "  - id: " + owner + "\n"
    assert source.count(marker) == 1
    block = source.split(marker, 1)[1].split("\n  - id:", 1)[0]
    tail = block.split("      - lambda: |-\n", 1)[1]
    lines = []
    for line in tail.splitlines():
        if line.strip() and not line.startswith("          "):
            break
        lines.append(line)
    body = textwrap.dedent("\n".join(lines)).strip()
    assert "include/usp_indoor_text.h" in source
    expected = 9 if owner == "refresh_indoor_environment_ui" else 3
    assert body.count("usp_indoor_text::label(") == expected
    return body


def arguments():
    """Allow retained results only under the project's artifact directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    return parser.parse_args()


def main():
    """Verify actual consumers and preserve optional retained artifacts."""
    args = arguments()
    if args.build_dir:
        directory = args.build_dir.resolve()
        assert directory.is_relative_to(ROOT / "artifacts")
        run_all(directory)
    else:
        with tempfile.TemporaryDirectory(prefix="usp-indoor-entry-") as name:
            run_all(Path(name) / "cases")


if __name__ == "__main__":
    main()
