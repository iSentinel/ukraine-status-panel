#!/usr/bin/env python3
"""Execute actual GT911/base touch methods against isolated I2C failures."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    """Copy a complete actual method without replacing its control flow."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def arguments():
    """Allow CI and local runs to select the pinned touchscreen source."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--touchscreen-source", type=Path)
    parser.add_argument("--compiler", default="g++")
    return parser.parse_args()


def touchscreen_source(explicit):
    """Use the installed ESPHome component rather than a copied implementation."""
    if explicit:
        return explicit
    binary = os.environ.get("ESPHOME_BIN") or shutil.which("esphome")
    if binary:
        prefix = Path(binary).resolve().parent.parent
        candidates = list(
            prefix.glob(
                "lib/python*/site-packages/esphome/components/touchscreen/touchscreen.cpp"
            )
        )
        if candidates:
            return candidates[0]
    raise FileNotFoundError("Set ESPHOME_BIN or --touchscreen-source")


def prepare(base):
    """Combine actual source methods with only host dependency replacements."""
    driver = (ROOT / "components/gt911/touchscreen/gt911_touchscreen.cpp").read_text()
    source = base.read_text()
    methods = [
        method(source, "void Touchscreen::loop()"),
        method(source, "void Touchscreen::send_touches_()"),
        method(source, "void Touchscreen::add_raw_touch_position_("),
        method(source, "int16_t Touchscreen::normalize_("),
    ]
    driver_methods = [
        method(driver, "bool GT911Touchscreen::communication_ok_("),
        method(driver, "bool GT911Touchscreen::read_touch_data_("),
        method(driver, "void GT911Touchscreen::update_touches()"),
    ]
    constants = driver[
        driver.index("static const char *const TAG") : driver.index(
            "void GT911Touchscreen::setup()"
        )
    ]
    content = (ROOT / "tests/gt911-fault-host.cpp.in").read_text()
    content = content.replace("@BASE_METHODS@", "\n".join(methods))
    content = content.replace("@DRIVER_METHODS@", "\n".join(driver_methods))
    return content.replace("@DRIVER_CONSTANTS@", constants)


def main(args):
    """Check all five failure positions without touching the panel or its bus."""
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise FileNotFoundError(args.compiler)
    with tempfile.TemporaryDirectory(prefix="usp-gt911-fault-") as directory:
        folder = Path(directory)
        source = folder / "actual-methods.cpp"
        source.write_text(prepare(touchscreen_source(args.touchscreen_source)))
        binary = folder / "fault-test"
        command = [
            compiler,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            str(source),
            "-o",
            str(binary),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    logger.info("GT911_FAULT_NATIVE=PASS actual methods; physical faults not injected")


if __name__ == "__main__":
    main(arguments())
