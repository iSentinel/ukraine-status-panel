#!/usr/bin/env python3
"""Check real nested LVGL code generation for bounded bootstrap progress."""
import argparse
import os
from pathlib import Path
import shutil
import re
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


FIXTURE = """esphome:
  name: lvgl-bootstrap-regression
  build_path: {build_path}
esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf
logger:
external_components:
  - source:
      type: local
      path: {components}
    components: [lvgl]
spi:
  clk_pin: 12
  mosi_pin: 11
display:
  - platform: ili9xxx
    model: ILI9341
    dc_pin: 10
    cs_pin: 9
    invert_colors: false
    id: test_display
    dimensions: 320x240
    auto_clear_enabled: false
    update_interval: never
lvgl:
  displays: [test_display]
  widgets:
    - obj:
        id: bootstrap_root
        widgets:
"""


def configuration(folder, components):
    """Generate source only; the fixture's SPI pins are never operated."""
    lines = FIXTURE.format(
        build_path=folder / "build", components=components
    ).splitlines()
    for index in range(8):
        lines.extend(
            [
                "          - button:",
                f"              id: bootstrap_button_{index}",
                "              widgets:",
                "                - label:",
                f'                    text: "Button {index}"',
            ]
        )
    return "\n".join(lines) + "\n"


def verify(source):
    """Check executed construction boundaries, not deferred callback text."""
    body = source.split("void setup() {", 1)[1].split("\nvoid loop()", 1)[0]
    feeds = body.count("App.feed_wdt();")
    gap = maximum = creates = 0
    for line in body.splitlines():
        if "App.feed_wdt();" in line:
            maximum, gap = max(maximum, gap), 0
        elif re.search(r"\blv_\w+_create\(", line):
            creates += 1
            gap += 1
    maximum = max(maximum, gap)
    assert feeds >= 34 and creates >= 17 and maximum <= 4, (
        feeds,
        creates,
        maximum,
    )
    assert "esp_task_wdt_deinit(" not in body
    assert "esp_task_wdt_reconfigure(" not in body
    logger.info(
        "LVGL_BOOTSTRAP_CODEGEN=PASS feeds={} creates={} max_gap={} "
        "nested_real_generator no_image no_device",
        feeds,
        creates,
        maximum,
    )


def main():
    """Generate C++ only; never compile, operate GPIO pins or contact hardware."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--components", type=Path, default=ROOT / "components")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="usp-lvgl-bootstrap-") as name:
        folder = Path(name)
        config = folder / "test.yaml"
        config.write_text(configuration(folder, args.components.resolve()))
        executable = os.environ.get(
            "ESPHOME_BIN", shutil.which("esphome") or "/opt/esphome/.venv/bin/esphome"
        )
        result = subprocess.run(
            [executable, "compile", str(config), "--only-generate"],
            env=dict(os.environ, PYTHONHASHSEED="0"),
            capture_output=True,
            text=True,
            timeout=90,
            check=False,
        )
        if result.returncode:
            logger.error(
                "Source-only fixture failed: {}",
                (result.stdout + result.stderr)[-4000:],
            )
            result.check_returncode()
        verify((folder / "build/src/main.cpp").read_text())


if __name__ == "__main__":
    main()
