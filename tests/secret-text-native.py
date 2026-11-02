#!/usr/bin/env python3
"""Check actual write-only text code with controlled storage and state callbacks."""

import argparse
import importlib.util
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
STUB_HEADERS = (
    "components/text/text.h",
    "core/automation.h",
    "core/component.h",
    "core/preferences.h",
    "core/helpers.h",
    "core/log.h",
)
LEGACY_CLASS = """
struct Function { bool has_value() { return false; } };
class LegacyText : public text::Text {
 public:
  Function f_;
  TextSaver<64> saver;
  TemplateTextSaverBase *pref_ = &saver;
  const char *initial_value_ = nullptr;
  void setup();
 protected:
  void control(const std::string &) override {}
};
"""


def framework_path():
    """Find the installed framework used for the old storage implementation."""
    specification = importlib.util.find_spec("esphome")
    if specification is not None:
        return Path(specification.origin).parent
    candidates = sorted(
        Path("/opt/esphome/.venv/lib").glob("python*/site-packages/esphome")
    )
    assert len(candidates) == 1, "Install ESPHome or pass --framework"
    return candidates[0]


def legacy_fixture(framework):
    """Use the actual old saver and setup method to verify key and byte format."""
    folder = framework / "components/template/text"
    header = (folder / "template_text.h").read_text()
    start = header.index("class TemplateTextSaverBase")
    end = header.index("class TemplateText final", start)
    source = (folder / "template_text.cpp").read_text()
    start_method = source.index("void TemplateText::setup()")
    opening = source.index("{", start_method)
    depth, end_method = 1, opening + 1
    while depth:
        depth += (source[end_method] == "{") - (source[end_method] == "}")
        end_method += 1
    method = source[start_method:end_method].replace(
        "TemplateText::setup", "LegacyText::setup"
    )
    return (
        '#pragma once\n#include "esphome/core/mock_shared.h"\n'
        "namespace esphome::template_ {\n"
        + "#pragma GCC diagnostic push\n"
        + '#pragma GCC diagnostic ignored "-Wunused-parameter"\n'
        + header[start:end]
        + "#pragma GCC diagnostic pop\n"
        + LEGACY_CLASS
        + method
        + "\n}\n"
    )


def prepare(directory, fixtures, framework):
    """Create minimal framework boundaries without replacing component logic."""
    base = directory / "esphome"
    shared = base / "core/mock_shared.h"
    shared.parent.mkdir(parents=True)
    shutil.copy2(fixtures / "mock_shared.h", shared)
    for name in STUB_HEADERS:
        path = base / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#pragma once\n#include "esphome/core/mock_shared.h"\n')
    (directory / "legacy_text_saver.h").write_text(legacy_fixture(framework))


def parse_arguments():
    """Describe the component, fixture and framework locations."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--component", type=Path, default=ROOT / "components/usp_secret_text"
    )
    parser.add_argument(
        "--fixtures", type=Path, default=ROOT / "tests/secret_text_host"
    )
    parser.add_argument("--framework", type=Path)
    return parser.parse_args()


def main():
    """Run actual component code under memory and behavior sanitizers."""
    args = parse_arguments()
    compiler = shutil.which("g++")
    assert compiler, "A host C++ compiler is required"
    with tempfile.TemporaryDirectory(prefix="usp-secret-text-") as temporary:
        directory = Path(temporary)
        prepare(directory, args.fixtures, args.framework or framework_path())
        binary = directory / "test"
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(directory),
            "-I",
            str(args.component),
            str(args.component / "secret_text.cpp"),
            str(args.fixtures / "cases.cpp"),
            "-o",
            str(binary),
        ]
        subprocess.run(command, check=True)
        subprocess.run(
            [str(binary)],
            check=True,
            env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1"),
        )
    logger.info("Secret text actual code: PASS; synthetic data, ASAN/UBSAN")


if __name__ == "__main__":
    main()
