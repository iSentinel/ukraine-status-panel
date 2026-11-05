#!/usr/bin/env python3
"""Test alarm switch boot with the complete installed ESPHome switch sources."""

import argparse
import hashlib
from importlib import metadata, util
import os
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def framework_root(explicit):
    """Find pinned installed ESPHome sources on either CI or the LXC."""
    if explicit:
        package = explicit
    else:
        version = metadata.version("esphome")
        if version != "2026.10.1":
            raise ValueError(f"Unexpected ESPHome version: {version}")
        specification = util.find_spec("esphome")
        if not specification or not specification.submodule_search_locations:
            raise ValueError("Installed ESPHome package was not found")
        package = Path(next(iter(specification.submodule_search_locations)))
    source = package / "components/template/switch/template_switch.cpp"
    if not source.is_file():
        raise ValueError("ESPHome switch source file is missing")
    return package


def restore_mode():
    """Read the production setting; omitted restore mode means ALWAYS_OFF."""
    text = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
    owning = text.split("  - id: alarm_sound_enabled\n", 1)[1]
    owning = owning.split("\n  - id:", 1)[0]
    assert 'initial_value: "true"' in owning
    assert "restore_value: yes" in owning
    block = text.split("    id: web_alarm_sound_enabled\n", 1)[1]
    block = block.split("\n  - platform:", 1)[0]
    mode = re.search(r"(?m)^    restore_mode: (\w+)$", block)
    value = mode.group(1) if mode else "ALWAYS_OFF"
    return {"ALWAYS_OFF": 0, "DISABLED": 8}[value]


def compile_command(package, binary):
    """Compile actual restore/control/setup code with platform-only substitutes."""
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
        str(ROOT / "tests/template_switch_host/shims"),
        "-I",
        str(package.parent),
        str(ROOT / "tests/alarm-switch-boot-native.cpp"),
        str(package / "components/switch/switch.cpp"),
        str(package / "components/template/switch/template_switch.cpp"),
        "-o",
        str(binary),
    ]


def execute(package, directory):
    """Keep compiler output, source identity and exact observed test outcome."""
    directory.mkdir(parents=True, exist_ok=False)
    binary = directory / "alarm-switch-boot"
    with (directory / "compile.log").open("w") as stream:
        result = subprocess.run(
            compile_command(package, binary), stdout=stream, stderr=stream
        )
    if result.returncode:
        logger.error("{}", (directory / "compile.log").read_text()[-3000:])
        result.check_returncode()
    source = package / "components/template/switch/template_switch.cpp"
    logger.info(
        "Framework setup source SHA256={}",
        hashlib.sha256(source.read_bytes()).hexdigest(),
    )
    result = subprocess.run(
        [str(binary), str(restore_mode())],
        capture_output=True,
        text=True,
        timeout=30,
    )
    (directory / "run.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        logger.error("{}", (directory / "run.log").read_text()[-3000:])
        result.check_returncode()
    logger.info("{}", result.stdout.strip())


def main():
    """Expose evidence and installed-source options; never contact the device."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--framework-root", type=Path)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    package = framework_root(args.framework_root)
    with TemporaryDirectory(prefix="usp-alarm-boot-") as temporary:
        execute(package, args.build_dir or Path(temporary) / "result")


if __name__ == "__main__":
    main()
