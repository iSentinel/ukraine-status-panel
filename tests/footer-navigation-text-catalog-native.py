#!/usr/bin/env python3
"""Check shared footer translations and preserve compiler diagnostics."""

from __future__ import annotations

import argparse
import re
from pathlib import Path
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
YAML = ROOT / "firmware/ukraine-status-panel.yaml"
FIXTURE = ROOT / "tests/fixtures/footer-navigation-text-catalog-golden.cpp"


def fixture() -> str:
    """Keep the exact accepted expected texts in an ordinary C++ source file."""
    return FIXTURE.read_text()


def arguments():
    """Offer help and an optional fresh directory for compiler evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    return parser.parse_args()


TARGETS = {
    "HOME": ["i18n_018", "i18n_033", "i18n_046", "i18n_058", "i18n_074"],
    "MAP": ["i18n_019", "i18n_034", "i18n_047", "i18n_059", "i18n_075"],
    "POWER": ["i18n_020", "i18n_035", "i18n_048", "i18n_060", "i18n_076"],
    "HISTORY": ["i18n_021", "i18n_036", "i18n_049", "i18n_061", "i18n_077"],
    "SETTINGS": ["i18n_022", "i18n_037", "i18n_050", "i18n_062", "i18n_078"],
}


def require_other_owners(text):
    """Keep non-footer captions with their own screen-specific catalogs."""
    assert not re.search(
        r"id: i18n_016\n\s+text: !lambda \|-\n" r"\s+return usp_footer_text::",
        text,
    )
    diagnostics_back = re.compile(
        r"id: i18n_080\n\s+text: !lambda \|-\n"
        r"\s+return usp_diagnostics_text::label\("
        r"id\(ui_language\), usp_diagnostics_text::Label::BACK\);"
    )
    assert len(diagnostics_back.findall(text)) == 3
    assert not re.search(
        r"id: i18n_080\n\s+text: !lambda \|-\n" r"\s+return usp_footer_text::",
        text,
    )
    assert not re.search(
        r"id: settings_title_label\n\s+text: !lambda \|-\n"
        r"\s+return usp_footer_text::",
        text,
    )


def require_consumers(text: str) -> None:
    """Require exactly the 25 bounded footer widgets to use the shared catalog."""
    assert "include/usp_footer_text.h" in text
    total = 0
    for label, widgets in TARGETS.items():
        expression = (
            "return usp_footer_text::label(id(ui_language), "
            f"usp_footer_text::Label::{label});"
        )
        for widget in widgets:
            pattern = re.compile(
                rf"id: {widget}\n\s+text: !lambda \|-\n\s+{re.escape(expression)}"
            )
            matches = pattern.findall(text)
            assert len(matches) == 3, (widget, label, len(matches))
            total += len(matches)
    assert total == 75, total
    require_other_owners(text)


def compiler_command(source, binary):
    """Enable memory checks while compiling the unchanged production header."""
    return [
        "g++",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-fno-pie",
        "-no-pie",
        "-I",
        str(ROOT / "include"),
        str(source),
        "-o",
        str(binary),
    ]


def execute(directory):
    """Keep complete diagnostic files; never overwrite an earlier result."""
    directory.mkdir(parents=True, exist_ok=False)
    source, binary = directory / "test.cpp", directory / "test"
    source.write_text(fixture())
    with (directory / "compile.log").open("w") as output:
        result = subprocess.run(
            compiler_command(source, binary),
            stdout=output,
            stderr=output,
            cwd=ROOT,
        )
    if result.returncode:
        logger.error("{}", (directory / "compile.log").read_text()[-2500:])
        result.check_returncode()
    result = subprocess.run(
        [str(binary)],
        capture_output=True,
        text=True,
        timeout=30,
    )
    (directory / "run.log").write_text(result.stdout + result.stderr)
    (directory / "run.exit").write_text(str(result.returncode) + "\n")
    if result.returncode:
        logger.error("{}", (directory / "run.log").read_text()[-2500:])
        result.check_returncode()
    logger.info("{}", result.stdout.strip())


def main():
    """Run the actual header with accepted text; no device action is performed."""
    args = arguments()
    require_consumers(YAML.read_text())
    with tempfile.TemporaryDirectory(prefix="usp-footer-i18n-") as temporary:
        execute(args.build_dir or Path(temporary) / "result")


if __name__ == "__main__":
    main()
