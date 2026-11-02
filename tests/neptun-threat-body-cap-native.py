#!/usr/bin/env python3
"""Exercise production NEPTUN threat-body streaming cap with large payloads."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

CPP = r'''
#include "neptun_threat_parser.h"
#include <cassert>
#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>

using namespace esphome::usp_neptun;

std::string make_body(size_t count) {
  std::ostringstream out;
  out << R"({"serverTime":"2026-10-22T11:00:00Z","threats":[)";
  for (size_t i = 0; i < count; ++i) {
    if (i) out << ',';
    out << "{\"id\":\"u" << i
        << "\",\"region\":\"Київ\",\"type\":\"uav\","
        << "\"status\":\"active\",\"advisory\":false,"
        << "\"note\":\"mass-attack-fixture-padding-0123456789-"
        << "abcdefghijklmnopqrstuvwxyz\"}";
  }
  out << "]}";
  return out.str();
}

ThreatScanner parse(const std::string &body) {
  ThreatScanner scanner;
  for (size_t pos = 0; pos < body.size(); pos += 2048) {
    const size_t count = std::min<size_t>(2048, body.size() - pos);
    scanner.feed(body.data() + pos, count);
  }
  return scanner;
}

int main() {
  const std::string mass = make_body(1400);
  assert(mass.size() > 131072 && mass.size() < 1048576);
  const auto accepted = parse(mass);
  assert(accepted.complete());
  assert(!accepted.result().overflow);
  assert(accepted.result().entries == 1400);
  assert(accepted.result().global.uav == 1400);

  const std::string excessive = make_body(9000);
  assert(excessive.size() > 1048576);
  const auto rejected = parse(excessive);
  assert(rejected.result().overflow);
  assert(!rejected.complete());

  std::cout << "NEPTUN_THREAT_BODY_CAP_NATIVE=PASS mass_bytes="
            << mass.size() << " entries=" << accepted.result().entries
            << " excessive_bytes=" << excessive.size()
            << " cap=1048576 fail_closed=1\n";
}
'''


def main() -> None:
    """Compile real production parser with sanitizers and run cap cases."""
    source = ROOT / "components/usp_neptun"
    with tempfile.TemporaryDirectory(prefix="usp-neptun-body-cap-") as temp:
        cpp = Path(temp) / "test.cpp"
        binary = Path(temp) / "test"
        cpp.write_text(CPP)
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-g", "-I", str(source), str(cpp),
            str(source / "neptun_threat_parser.cpp"),
            str(source / "neptun_alert_parser.cpp"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
