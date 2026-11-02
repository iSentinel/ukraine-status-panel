#!/usr/bin/env python3
"""Compare current Room summary formatting against the fixed original body."""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import subprocess
from tempfile import TemporaryDirectory
import textwrap

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/fixtures/room-summary"
SOURCE = ROOT / "firmware/ukraine-status-panel.yaml"
HEADER = ROOT / "include/usp_room_summary_text.h"


def digest(path):
    """Hash a canonical input without changing it."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def current_body():
    """Read the current formatter rather than an unlinked test copy."""
    text = SOURCE.read_text()
    assert "include/usp_room_summary_text.h" in text
    start = text.index("  - id: refresh_room_graph_ui\n")
    end = text.index("\n  - id:", start + 10)
    section = text[start:end]
    start = section.index("          const uint8_t lang = id(ui_language)")
    summary = section.index("          char summary[96];", start)
    end = section.index(
        "          usp_lcd_navigation::label_text(id(room_history_summary)",
        summary,
    )
    # Keep this test focused on the summary formatter. Period presentation
    # setup is covered independently by room-period-ui-static/native tests.
    body = section[start:section.index("          const auto period =", start)]
    body += section[summary:end]
    body = textwrap.dedent(body)
    assert body.count("usp_room_summary_text::format(lang)") == 1
    assert "id(ui_language) < 3 ? id(ui_language) : 0" in body
    assert "id(room_graph_valid_count)" in body
    assert "id(room_graph_gap_count)" in body
    return body


def variants(header, body):
    """Keep the original oracle fixed and alter one candidate property."""
    values = [
        json.loads(row["before"])
        for row in json.loads((FIXTURES / "mapping.json").read_text())
    ]
    ua, ru = (json.dumps(value, ensure_ascii=False) for value in values[:2])
    swapped = (
        header.replace(ua, "@@UA@@").replace(ru, ua).replace("@@UA@@", ru)
    )
    yield "positive", header, body
    yield "caption", header.replace("вимірів", "wrong", 1), body
    yield "numeric_type", header.replace("%u", "%x", 1), body
    yield "fallback", header.replace(
        "language < 2 ? language : 2", "language < 2 ? language : 0"
    ), body
    yield "locale_order", swapped, body
    yield "language_argument", header, body.replace(
        "::format(lang)",
        "::format(static_cast<uint8_t>(lang == 0 ? 2 : lang))",
        1,
    )
    yield "saved_language_guard", header, body.replace(
        "id(ui_language) < 3 ? id(ui_language) : 0",
        "id(ui_language) < 3 ? id(ui_language) : 2",
        1,
    )


def compile_case(source, target, compiler):
    """A compiler failure never counts as a rejected semantic mutation."""
    command = [
        compiler,
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-fno-pie",
        "-no-pie",
        str(source),
        "-o",
        str(target / "test"),
    ]
    compiled = subprocess.run(
        command, capture_output=True, text=True, timeout=60
    )
    (target / "compile.log").write_text(compiled.stdout + compiled.stderr)
    compiled.check_returncode()


def run_case(directory, name, header, body, compiler):
    """Run the copied formatter with ordinary and boundary count values."""
    target = directory / name
    target.mkdir()
    (target / "usp_room_summary_text.h").write_text(header)
    text = (FIXTURES / "fixture.cpp.in").read_text()
    text = text.replace(
        "@@BEFORE@@", (FIXTURES / "body-before.cpp").read_text()
    )
    text = text.replace("@@AFTER@@", body)
    source = target / "test.cpp"
    source.write_text(text)
    compile_case(source, target, compiler)
    result = subprocess.run(
        [str(target / "test")],
        cwd=target,
        capture_output=True,
        text=True,
        timeout=30,
    )
    output = result.stdout + result.stderr
    (target / "run.log").write_text(output)
    (target / "run.exit").write_text(str(result.returncode) + "\n")
    if name == "positive":
        assert result.returncode == 0
        assert "getter=256 callbacks=20736" in output
    else:
        assert result.returncode != 0, name
        assert any(
            word in output
            for word in ("Assertion", "AddressSanitizer", "runtime error")
        ), name
    return dict(name=name, compile_exit=0, run_exit=result.returncode)


def check_all(directory, compiler):
    """Keep YAML and header identities stable throughout the artifact tests."""
    before = {str(path): digest(path) for path in (SOURCE, HEADER)}
    directory.mkdir(parents=True)
    header, body = HEADER.read_text(), current_body()
    results = [
        run_case(directory, name, text, current, compiler)
        for name, text, current in variants(header, body)
    ]
    assert {str(path): digest(path) for path in (SOURCE, HEADER)} == before
    report = dict(
        status="PASS",
        input_sha256=before,
        getter_comparisons=256,
        callback_comparisons=20736,
        negative_controls=6,
        results=results,
        production_modified=False,
        device_contact=False,
    )
    with (directory / "report.json").open("x") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    logger.info(
        "ROOM_SUMMARY_NATIVE=PASS getter256 callback20736 negative6 no_device"
    )


def main():
    """Retain optional evidence only under the project artifacts directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    artifacts = ROOT / "artifacts"
    artifacts.mkdir(exist_ok=True)
    if args.build_dir and not args.build_dir.resolve().is_relative_to(
        artifacts
    ):
        parser.error("--build-dir must be a new path under artifacts/")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    with TemporaryDirectory(prefix="room-summary-", dir=artifacts) as name:
        directory = args.build_dir.resolve() if args.build_dir else Path(name) / "cases"
        check_all(directory, args.compiler)


if __name__ == "__main__":
    main()
