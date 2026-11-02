#!/usr/bin/env python3
"""Execute production NEPTUN scheduling methods with a controlled clock."""
import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
SUPPORT = r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#define ESP_LOGD(...) ((void)0)
#define YESNO(value) ((value) ? "YES" : "NO")
static uint32_t now;
static uint32_t millis() { return now; }
class NeptunClient {
 public:
  struct Snapshot { uint16_t total{}, threat_entries{}; };
  Snapshot state{};
  uint32_t retry_delay_ms_{60000}, next_refresh_ms_{};
  uint32_t refresh_interval_ms_{5000}, idle_refresh_interval_ms_{20000};
  static constexpr uint32_t MIN_RETRY_MS = 15000;
  static constexpr uint32_t MAX_RETRY_MS = 120000;
  static constexpr uint32_t GATE_BUSY_RETRY_MS = 2000;
  Snapshot snapshot_copy_() const { return state; }
  void schedule_success_();
  void schedule_retry_();
  void schedule_gate_retry_();
};
"""
CHECKS = r"""
int main() {
  for (uint32_t time : {1234U, 0xfffffff0U}) {
    for (uint16_t entries : {uint16_t(0), uint16_t(4)}) {
      NeptunClient client;
      now = time;
      client.state = {0, entries};
      client.schedule_success_();
      const uint32_t expected = entries ? 5000U : 20000U;
      assert(client.next_refresh_ms_ == uint32_t(time + expected));
      assert(client.retry_delay_ms_ == 15000U);
    }
  }
  NeptunClient retry;
  now = 1000;
  retry.schedule_retry_();
  assert(retry.next_refresh_ms_ == 61000 && retry.retry_delay_ms_ == 120000);
  retry.schedule_retry_();
  assert(retry.next_refresh_ms_ == 121000 && retry.retry_delay_ms_ == 120000);
  NeptunClient busy;
  busy.retry_delay_ms_ = 60000;
  now = 0xfffffff0U;
  busy.schedule_gate_retry_();
  assert(busy.next_refresh_ms_ == uint32_t(now + 2000U));
  assert(busy.retry_delay_ms_ == 60000U);
  std::puts("NEPTUN_REFRESH_NATIVE=PASS success raw_entries retry_backoff gate_busy2s no_backoff_growth rollover");
}
"""


def scheduling_methods() -> str:
    """Extract the actual three production scheduling method bodies."""
    text = (ROOT / "components/usp_neptun/neptun_client.cpp").read_text()
    start = text.index("void NeptunClient::schedule_success_() {")
    end = text.index("NeptunClient::Snapshot NeptunClient::snapshot_copy_()", start)
    return text[start:end]


def check(directory: str) -> None:
    """Compile actual methods with ASAN/UBSAN and a controlled clock."""
    path = Path(directory)
    source = path / "refresh.cpp"
    source.write_text(SUPPORT + scheduling_methods() + CHECKS)
    binary = path / "refresh"
    subprocess.run(
        [
            shutil.which("g++") or "g++",
            "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            str(source), "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    logger.info("{}", result.stdout.strip())


def main() -> None:
    """Run offline; no network or hardware access is required."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    with TemporaryDirectory(prefix="usp-refresh-native-") as directory:
        check(directory)


if __name__ == "__main__":
    main()
