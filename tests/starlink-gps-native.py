"""Test proto3 GPS defaults and malformed data using production C++ parsing."""

import argparse
import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r"""
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
#include "starlink_grpc.h"
using namespace esphome::usp_starlink;
using Bytes = std::vector<uint8_t>;
static void varint(Bytes &out, uint64_t value) {
  do {
    uint8_t byte = value & 127;
    value >>= 7;
    out.push_back(byte | (value ? 128 : 0));
  } while (value);
}
static Bytes nested(uint32_t field, const Bytes &inner) {
  Bytes out;
  varint(out, uint64_t(field) * 8 + 2);
  varint(out, inner.size());
  out.insert(out.end(), inner.begin(), inner.end());
  return out;
}
static Bytes frame(const Bytes &gps) {
  auto root = nested(2004, nested(1015, gps));
  Bytes out{0, uint8_t(root.size() >> 24), uint8_t(root.size() >> 16),
            uint8_t(root.size() >> 8), uint8_t(root.size())};
  out.insert(out.end(), root.begin(), root.end());
  return out;
}
"""
CASES = r"""
static Bytes raw_frame(const Bytes &root) {
  Bytes out{0, uint8_t(root.size() >> 24), uint8_t(root.size() >> 16),
            uint8_t(root.size() >> 8), uint8_t(root.size())};
  out.insert(out.end(), root.begin(), root.end());
  return out;
}
static void append(Bytes &target, const Bytes &extra) {
  target.insert(target.end(), extra.begin(), extra.end());
}
static void rejected(const Bytes &body) {
  DishGpsStatus value{true, true, true, 77, 13};
  assert(!parse_gps_status(body, &value));
  assert(value.parsed && value.gps_valid && value.inhibit_gps);
  assert(value.gps_sats == 77 && value.grpc_status == 13);
}
static void defaults_and_merge() {
  DishGpsStatus value{};
  assert(parse_gps_status(frame({8, 1, 16, 12}), &value));
  assert(value.parsed && value.gps_valid && value.gps_sats == 12);
  assert(!value.inhibit_gps);
  assert(parse_gps_status(frame({}), &value));
  assert(!value.gps_valid && !value.inhibit_gps && value.gps_sats == 0);
  assert(parse_gps_status(frame({32, 1, 32, 0}), &value));
  assert(!value.inhibit_gps);
  Bytes dish = nested(1015, {8, 1, 32, 1});
  append(dish, nested(1015, {16, 12}));
  assert(parse_gps_status(raw_frame(nested(2004, dish)), &value));
  assert(value.gps_valid && value.inhibit_gps && value.gps_sats == 12);
  Bytes root = nested(2004, nested(1015, {32, 1}));
  append(root, nested(2004, nested(1015, {})));
  assert(parse_gps_status(raw_frame(root), &value));
  assert(value.inhibit_gps);
  assert(parse_gps_status(frame({32, 2}), &value));
  assert(value.inhibit_gps);
}
static void malformed_fields() {
  for (Bytes gps : {Bytes{0}, Bytes{8}, Bytes{34, 0}, Bytes{16, 128},
                    Bytes{15}, Bytes{9, 0}, Bytes{42, 3, 1},
                    Bytes{32, 1, 0}, Bytes{8, 1, 16, 12, 32, 1, 15}}) {
    rejected(frame(gps));
  }
  Bytes overflow{32};
  overflow.insert(overflow.end(), 9, 255);
  overflow.push_back(2);
  rejected(frame(overflow));
  Bytes sats{16};
  varint(sats, uint64_t(UINT32_MAX) + 1);
  rejected(frame(sats));
  Bytes invalid_key;
  varint(invalid_key, uint64_t(0x20000000) * 8);
  invalid_key.push_back(0);
  rejected(frame(invalid_key));
  Bytes gps{32};
  gps.insert(gps.end(), 9, 255);
  gps.push_back(1);
  DishGpsStatus value{};
  assert(parse_gps_status(frame(gps), &value) && value.inhibit_gps);
}
static void containers_and_frames() {
  rejected(raw_frame({}));
  rejected(raw_frame(nested(2004, {})));
  rejected(raw_frame(nested(2003, nested(1015, {}))));
  rejected(raw_frame(nested(2004, nested(1014, {}))));
  Bytes valid = frame({8, 1, 16, 12, 32, 0});
  for (size_t length = 0; length < valid.size(); ++length)
    rejected(Bytes(valid.begin(), valid.begin() + length));
  Bytes extra = valid;
  extra.push_back(0);
  rejected(extra);
  extra = valid;
  extra[0] = 1;
  rejected(extra);
  Bytes root = nested(2004, nested(1015, {32, 1}));
  root.push_back(0);
  rejected(raw_frame(root));
  Bytes dish = nested(1015, {32, 1});
  dish.push_back(0);
  rejected(raw_frame(nested(2004, dish)));
  rejected(raw_frame({0xA0, 0x7D, 0}));
  assert(!parse_gps_status(valid, nullptr));
}
static void unknown_fields() {
  Bytes gps{8, 1, 16, 12, 32, 1};
  varint(gps, 99 * 8);
  varint(gps, 123);
  append(gps, nested(100, {0, 255}));
  varint(gps, 101 * 8 + 5);
  gps.insert(gps.end(), 4, 255);
  varint(gps, 102 * 8 + 1);
  gps.insert(gps.end(), 8, 255);
  DishGpsStatus value{};
  assert(parse_gps_status(frame(gps), &value));
  assert(value.gps_valid && value.inhibit_gps && value.gps_sats == 12);
}
int main() {
  defaults_and_merge();
  malformed_fields();
  containers_and_frames();
  unknown_fields();
  std::cout << "STARLINK_PROTO3_DEFAULTS=PASS default_false merge bounds "
               "malformed atomic_output ASAN_UBSAN" << std::endl;
}
"""


def production_decoder():
    """Extract exact methods; never copy their decisions into the test."""
    source = (ROOT / "components/usp_starlink/starlink_grpc.cpp").read_text()
    start = source.index("bool read_varint(")
    end = source.index("\n}  // namespace", start)
    return source[start:end]


def run_native():
    """Compile isolated parsing with sanitizers and report real assertions."""
    with tempfile.TemporaryDirectory(prefix="usp-starlink-defaults-") as name:
        work = Path(name)
        source = work / "test.cpp"
        binary = work / "test"
        source.write_text(FIXTURE + production_decoder() + CASES)
        command = [
            "g++",
            "-std=c++20",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-fno-pie",
            "-no-pie",
            "-I",
            str(ROOT / "components/usp_starlink"),
            str(source),
            "-o",
            str(binary),
        ]
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary)], check=False)
        logger.info("Native production parser exit: {}", result.returncode)
        return result.returncode


def main():
    """Run a server-only reproduction without device or network access."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    raise SystemExit(0 if run_native() == 0 else 1)


if __name__ == "__main__":
    main()
