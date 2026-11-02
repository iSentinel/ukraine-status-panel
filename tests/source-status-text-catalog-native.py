#!/usr/bin/env python3
"""Compare copied source-status text callbacks without changing firmware."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import resource
import textwrap
from tempfile import TemporaryDirectory
import subprocess

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/fixtures/source-status"
SOURCE = ROOT / "firmware/ukraine-status-panel.yaml"
MODULES = ("usp_wifi_status_text", "usp_neptun_status_text")


def digest(path):
    """Hash exact source bytes before and after all disposable cases."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def callback(text, name):
    """Select one exact script without matching a longer identifier."""
    start = text.index("  - id: " + name + "\n")
    end = text.index("\n  - id:", start + 10)
    return text[start:end]


def substitutions():
    """Compile the current YAML text handler against its fixed baseline."""
    text = SOURCE.read_text()
    wifi = callback(text, "refresh_wifi_icon")
    match = re.search(r"const char \*status = connected(.*?);", wifi, re.S)
    assert match is not None, "Wi-Fi status expression missing"
    selected = callback(text, "refresh_selected_neptun_ui")
    body = selected.split("text: !lambda |-\n", 1)[1].split(
        "\n      - lambda:", 1
    )[0]

    return {
        "@@WIFI_BEFORE@@": (
            FIXTURES / "wifi-expression-before.cpp"
        ).read_text(),
        "@@WIFI_AFTER@@": "connected" + match.group(1),
        "@@SELECTED_BEFORE@@": (
            FIXTURES / "selected-body-before.cpp"
        ).read_text(),
        "@@SELECTED_AFTER@@": textwrap.dedent(body),
    }


def require_consumers(text):
    """Require each catalog binding inside its actual owning callback."""
    for namespace in MODULES:
        assert "include/" + namespace + ".h" in text
    rows = json.loads((FIXTURES / "mapping.json").read_text())
    for row in rows:
        assert callback(text, row["owner"]).count(row["after"]) == 1, row[
            "key"
        ]


def header_changes(namespace, header):
    """Make four compiled negative controls for one catalog."""
    rows = [
        row
        for row in json.loads((FIXTURES / "mapping.json").read_text())
        if row["namespace"] == namespace
    ]
    first, second = (row["key"] for row in rows[:2])
    caption = json.dumps(rows[0]["values"][2], ensure_ascii=False)
    changes = {
        "named_key": header.replace(first + " = 0", first + " = 1").replace(
            second + " = 1", second + " = 0"
        ),
        "caption": header.replace(caption, '"wrong-caption"', 1),
        "fallback": header.replace(
            "language < 2 ? language : 2", "language < 2 ? language : 0"
        ),
        "overbound": header.replace(
            f"index < {len(rows)} ?", f"index < {len(rows) + 1} ?"
        ),
    }
    assert all(value != header for value in changes.values())
    return changes


def cases(headers, parts):
    """Keep the positive oracle unchanged while varying one candidate input."""
    yield "positive", headers, parts
    for namespace, header in headers.items():
        for kind, changed in header_changes(namespace, header).items():
            candidate = dict(headers)
            candidate[namespace] = changed
            yield namespace + "-" + kind, candidate, parts
    changes = (
        ("cached_guard", "@@SELECTED_AFTER@@", "if (cached)", "if (!cached)"),
        (
            "truncation_guard",
            "@@SELECTED_AFTER@@",
            "shown >= 3U",
            "shown >= 4U",
        ),
        ("wifi_guard", "@@WIFI_AFTER@@", "connected", "!connected"),
    )
    for name, key, before, after in changes:
        candidate = dict(parts)
        assert before in candidate[key]
        candidate[key] = candidate[key].replace(before, after, 1)
        yield name, headers, candidate


def compile_case(source, target, compiler):
    """Compile every control successfully before running its assertions."""
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


def run_case(directory, name, headers, parts, compiler):
    """Require successful compilation before interpreting any negative result."""
    target = directory / name
    target.mkdir()
    for namespace, text in headers.items():
        (target / (namespace + ".h")).write_text(text)
    text = (FIXTURES / "fixture.cpp.in").read_text()
    for marker, body in parts.items():
        text = text.replace(marker, body)
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
        assert result.returncode == 0, name
        assert "selected=131072 wifi=512" in output
    else:
        assert result.returncode != 0, name
        assert any(
            word in output
            for word in ("Assertion", "AddressSanitizer", "runtime error")
        ), name
    logger.info("{} compiled=PASS result={}", name, result.returncode)
    return dict(name=name, compile_exit=0, run_exit=result.returncode)


def check_all(directory, compiler):
    """Run controls only on copied headers and preserve the canonical hash."""
    before = digest(SOURCE)
    require_consumers(SOURCE.read_text())
    directory.mkdir(parents=True)
    headers = {
        name: (ROOT / "include" / (name + ".h")).read_text()
        for name in MODULES
    }
    results = [
        run_case(directory, name, header, parts, compiler)
        for name, header, parts in cases(headers, substitutions())
    ]
    assert digest(SOURCE) == before
    report = dict(
        status="PASS",
        source_sha256_before=before,
        source_sha256_after=digest(SOURCE),
        named_key_comparisons=1792,
        invalid_key_checks=1024,
        selected_callback_comparisons=131072,
        wifi_comparisons=512,
        negative_controls=len(results) - 1,
        results=results,
        production_modified=False,
        device_contact=False,
    )
    with (directory / "report.json").open("x") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    logger.info(
        "SOURCE_STATUS_NATIVE=PASS selected131072 WiFi512 negative11; no device"
    )


def main():
    """Use disposable artifacts by default or retain a new chosen directory."""
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
    with TemporaryDirectory(prefix="source-status-", dir=artifacts) as name:
        directory = args.build_dir.resolve() if args.build_dir else Path(name) / "cases"
        check_all(directory, args.compiler)


if __name__ == "__main__":
    main()
