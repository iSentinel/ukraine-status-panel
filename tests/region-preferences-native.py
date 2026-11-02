#!/usr/bin/env python3
"""Execute the actual local region component with a controlled preference store."""
import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
PREFERENCES = r"""
#pragma once
#include <cassert>
#include <cstdint>
namespace esphome {
inline bool stored=false, save_ok=true;
inline uint16_t stored_value=31;
inline uint32_t writes=0, actual_key=0;
class ESPPreferenceObject {
 public:
  bool load(uint16_t *out) {
    if (!stored) return false;
    *out=stored_value; return true;
  }
  bool save(const uint16_t *value) {
    if (!save_ok) return false;
    stored=true; stored_value=*value; ++writes; return true;
  }
};
struct Preferences {
  template<class T> ESPPreferenceObject make_preference(uint32_t key, bool flash) {
    static_assert(sizeof(T)==2);
    assert(flash); actual_key=key; return {};
  }
};
inline Preferences prefs;
inline Preferences *global_preferences=&prefs;
}
"""
CHECKS = r"""
#include "region_settings.h"
#include <cstdio>
using namespace esphome;
using esphome::usp_region::RegionSettings;
int main() {
  uint32_t legacy=2166136261U;
  for (const unsigned char c : "usp_jaam_selected_region_v1") {
    if (c) legacy=(legacy^c)*16777619U;
  }
  stored=true; stored_value=31;
  RegionSettings first; first.setup();
  assert(actual_key==legacy && first.selected_region_id()==31 && writes==0);
  assert(first.select_region(14) && stored_value==14 && writes==1);
  assert(first.select_region(14) && writes==1);
  assert(!first.select_region(0) && !first.select_region(65535) && writes==1);
  save_ok=false;
  assert(!first.select_region(31) && first.selected_region_id()==14);
  save_ok=true;
  RegionSettings reboot; reboot.setup();
  assert(reboot.selected_region_id()==14 && writes==1);
  stored_value=65535;
  RegionSettings invalid; invalid.setup();
  assert(invalid.selected_region_id()==31 && writes==1);
  stored=false;
  RegionSettings blank; blank.setup();
  assert(blank.selected_region_id()==31 && writes==1);
  std::puts("REGION_NATIVE=PASS legacy_key restore no_op invalid save_failure no_global_sync");
}
"""


def check(directory):
    """Compile unchanged component code; model only ESPHome storage and logging."""
    path = Path(directory)
    core = path / "esphome/core"
    core.mkdir(parents=True)
    (core / "component.h").write_text(
        "#pragma once\nnamespace esphome {class Component {public: "
        "virtual void setup() {} virtual ~Component()=default;};}\n"
    )
    (core / "preferences.h").write_text(PREFERENCES)
    (core / "log.h").write_text(
        "#pragma once\n#define ESP_LOGI(tag,...) ((void)(tag))\n"
        "#define ESP_LOGW(tag,...) ((void)(tag))\n"
    )
    source = path / "check.cpp"
    source.write_text(CHECKS)
    binary = path / "region"
    component = ROOT / "components/usp_region"
    subprocess.run(
        [
            shutil.which("g++") or "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(path),
            "-I",
            str(component),
            "-I",
            str(ROOT / "include"),
            str(source),
            str(component / "region_settings.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    logger.info("{}", result.stdout.strip())


def main():
    """Run offline with no device writes or network access."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    with TemporaryDirectory(prefix="usp-region-native-") as directory:
        check(directory)


if __name__ == "__main__":
    main()
