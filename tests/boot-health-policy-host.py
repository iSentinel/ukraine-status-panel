#!/usr/bin/env python3
"""Exercise actual safe-mode methods with controlled clock and flash boundaries."""

import argparse
import importlib.util
import shutil
import subprocess
import tempfile
from pathlib import Path

import yaml
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def source_path():
    """Find the installed framework without requiring one Python version."""
    specification = importlib.util.find_spec("esphome")
    if specification is not None:
        package = Path(specification.origin).parent
        return package / "components/safe_mode/safe_mode.cpp"
    candidates = sorted(
        Path("/opt/esphome/.venv/lib").glob(
            "python*/site-packages/esphome/components/safe_mode/safe_mode.cpp"
        )
    )
    assert len(candidates) == 1, "Install ESPHome or pass --source"
    return candidates[0]


def extract(source, name):
    """Copy the complete upstream method body without rewriting its logic."""
    marker = "void SafeModeComponent::" + name + "()"
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def fixture(source):
    """Provide clock, RTC and image-confirmation boundaries for actual methods."""
    prefix = r"""
#include <cassert>
#include <cstdint>
#include <iostream>
#define ESP_LOGI(...) ((void)0)
constexpr const char *TAG = "test";
struct Clock {
  uint32_t now = 0;
  uint32_t get_loop_component_start_time() { return now; }
} App;
class SafeModeComponent {
 public:
  static constexpr uint32_t ENTER_SAFE_MODE_MAGIC = 0x5afe;
  uint32_t rtc = 1, safe_mode_start_time_ = 0;
  uint32_t safe_mode_boot_is_good_after_ = 60000;
  bool boot_successful_ = false;
  unsigned cleaned = 0, confirmed = 0, disabled = 0;
  uint32_t read_rtc_() { return rtc; }
  void clean_rtc() { ++cleaned; rtc = 0; }
  void confirm_app_image_() { ++confirmed; }
  void disable_loop() { ++disabled; }
  void on_safe_shutdown();
  void mark_successful();
  void loop();
};
"""
    methods = "\n".join(
        extract(source, n) for n in ("on_safe_shutdown", "mark_successful", "loop")
    )
    return prefix + methods + TESTS


TESTS = r"""
int main() {
  SafeModeComponent early;
  early.on_safe_shutdown();
  assert(early.cleaned == 1 && !early.boot_successful_);
  std::cout << "EARLY_CONFIRM=" << early.confirmed << std::endl;
  assert(early.confirmed == 0);
  SafeModeComponent manual;
  manual.rtc = SafeModeComponent::ENTER_SAFE_MODE_MAGIC;
  manual.on_safe_shutdown();
  assert(manual.cleaned == 0 && manual.rtc == manual.ENTER_SAFE_MODE_MAGIC);
  assert(manual.confirmed == 0);
  SafeModeComponent timed;
  App.now = 59999; timed.loop();
  App.now = 60000; timed.loop();
  assert(timed.confirmed == 0);
  App.now = 60001; timed.loop();
  assert(timed.boot_successful_ && timed.confirmed == 1);
  assert(timed.cleaned == 1 && timed.disabled == 1);
  App.now = 90000; timed.loop();
  assert(timed.confirmed == 1);
  SafeModeComponent wrapped;
  wrapped.safe_mode_start_time_ = UINT32_MAX - 30000;
  App.now = 29999; wrapped.loop();
  assert(wrapped.confirmed == 0);
  App.now = 30000; wrapped.loop();
  assert(wrapped.confirmed == 1);
  std::cout << "BOOT_HEALTH_POLICY=PASS clock/RTC/image boundaries" << std::endl;
}
"""


def main():
    """Compile actual upstream methods under the policy selected by YAML."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        default=ROOT / "firmware/ukraine-status-panel-production.yaml",
    )
    parser.add_argument("--source", type=Path, help="Actual safe_mode.cpp")
    args = parser.parse_args()
    config = yaml.load(args.config.read_text(), Loader=yaml.BaseLoader)
    enabled = config["safe_mode"]["boot_is_good_on_shutdown"] == "true"
    compiler = shutil.which("g++")
    assert compiler, "A host C++ compiler is required"
    with tempfile.TemporaryDirectory(prefix="usp-boot-health-") as directory:
        cpp, binary = Path(directory) / "test.cpp", Path(directory) / "test"
        cpp.write_text(fixture((args.source or source_path()).read_text()))
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DUSE_OTA_ROLLBACK",
            str(cpp),
            "-o",
            str(binary),
        ]
        if enabled:
            command.insert(1, "-DUSE_SAFE_MODE_BOOT_IS_GOOD_ON_SHUTDOWN")
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary)], check=False)
        assert result.returncode == 0, "Image was confirmed before healthy boot"
    logger.info("Actual safe-mode methods: PASS; shutdown confirmation disabled")


if __name__ == "__main__":
    main()
