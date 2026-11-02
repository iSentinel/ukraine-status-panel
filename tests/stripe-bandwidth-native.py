#!/usr/bin/env python3
"""Test the real stripe probe with memory checks and controlled host clocks."""
import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
HEAP = """
#pragma once
#include <cstdlib>
#include <cstdint>
#include <cstddef>
constexpr uint32_t MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2;
inline size_t host_available = 52000, host_largest = 52000, host_bytes = 0;
inline unsigned host_allocations = 0, host_live_allocations = 0;
inline bool host_fail_allocation = false;
inline size_t heap_caps_get_free_size(uint32_t) { return host_available; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return host_largest; }
inline void *heap_caps_malloc(size_t bytes, uint32_t) {
  ++host_allocations;
  if (host_fail_allocation) return nullptr;
  void *value = std::malloc(bytes);
  if (value) { host_bytes = bytes; host_available -= bytes; ++host_live_allocations; }
  return value;
}
inline void heap_caps_free(void *value) {
  std::free(value);
  host_available += host_bytes;
  --host_live_allocations;
}
"""
HAL = """
#pragma once
#include <cstdint>
inline uint32_t host_tick = 0, host_step = 5;
namespace esphome {
inline uint32_t micros() { host_tick += host_step; return host_tick; }
}
"""


def check(directory, output):
    """Compile the production helper against only the external API shims."""
    directory = Path(directory)
    core = directory / "esphome/core"
    core.mkdir(parents=True)
    (directory / "esp_heap_caps.h").write_text(HEAP)
    (core / "hal.h").write_text(HAL)
    (core / "log.h").write_text('#pragma once\n#define ESP_LOGI(...) ((void)0)\n')
    compiler = shutil.which("g++")
    if compiler is None:
        raise RuntimeError("Install g++ to run the native diagnostic gate")
    binary = directory / "stripe-probe"
    command = [
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
        "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(directory), "-I", str(ROOT / "include"),
        str(ROOT / "tests/stripe-bandwidth-native.cpp"), "-o", str(binary),
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    text = result.stdout + result.stderr
    if result.returncode == 0:
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        text += result.stdout + result.stderr
    if output:
        output.write_text(text)
    if result.returncode:
        raise RuntimeError(text)
    logger.info("{}", text.strip())


def main():
    """Provide CLI help and use a temporary build directory by default."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Save full compiler/test output")
    args = parser.parse_args()
    with TemporaryDirectory(prefix="usp-stripe-probe-") as directory:
        check(directory, args.output)


if __name__ == "__main__":
    main()
