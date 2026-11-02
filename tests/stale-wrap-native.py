#!/usr/bin/env python3
"""Execute NEPTUN/Outage production stale methods across uint32 millis wrap."""
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def method(path: Path, signature: str, next_signature: str) -> str:
    text = path.read_text()
    start = text.index(signature)
    end = text.index(next_signature, start)
    return text[start:end]


def build_source() -> str:
    neptun = method(
        ROOT / "components/usp_neptun/neptun_client.cpp",
        "bool NeptunClient::stale() const {",
        "bool NeptunClient::last_fetch_ok() const",
    )
    outage = method(
        ROOT / "components/usp_outage/outage_client.cpp",
        "bool OutageClient::schedule_stale() const {",
        "bool OutageClient::last_fetch_ok() const",
    )
    support = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <mutex>
static uint32_t fake_ms;
static uint32_t millis() { return fake_ms; }
class NeptunClient {
 public:
  struct Snapshot { bool valid{true}; uint32_t received_ms{}; };
  mutable std::mutex state_mutex_;
  Snapshot snapshot_{};
  uint32_t stale_after_ms_{3000};
  bool stale() const;
};
class OutageClient {
 public:
  struct Snapshot { bool valid{true}; uint32_t received_ms{}; };
  mutable std::mutex state_mutex_;
  Snapshot snapshot_{};
  uint32_t stale_after_ms_{3000};
  bool day_current_(const int &, bool) const { return snapshot_.valid; }
  int today_stub_{};
  bool schedule_stale() const;
};
"""
    # Preserve the exact subtraction/threshold bodies; adapt only the Outage
    # synthetic snapshot member name required by its day_current_ call.
    outage = outage.replace("this->snapshot_.today", "this->today_stub_")
    checks = r"""
static void neptun_wrap() {
  NeptunClient c;
  c.snapshot_.received_ms = UINT32_MAX - 1000U;
  fake_ms = 1000U; assert(!c.stale());       // elapsed 2001
  fake_ms = 1999U; assert(!c.stale());       // elapsed 3000 exact boundary
  fake_ms = 2000U; assert(c.stale());        // elapsed 3001
  c.snapshot_.valid = false; assert(c.stale());
}
static void outage_wrap() {
  OutageClient c;
  c.snapshot_.received_ms = UINT32_MAX - 1000U;
  fake_ms = 1000U; assert(!c.schedule_stale());
  fake_ms = 1999U; assert(!c.schedule_stale());
  fake_ms = 2000U; assert(c.schedule_stale());
  c.snapshot_.valid = false; assert(c.schedule_stale());
}
int main() {
  neptun_wrap(); outage_wrap();
  std::puts("STALE_WRAP_NATIVE=PASS actual_methods=2 boundary_gt=1 wrap_fresh=1 wrap_stale=1 invalid_stale=1");
}
"""
    return support + neptun + outage + checks


def main() -> None:
    with TemporaryDirectory(prefix="usp-stale-wrap-") as directory:
        source = Path(directory) / "stale.cpp"
        binary = Path(directory) / "stale"
        source.write_text(build_source())
        subprocess.run([
            shutil.which("g++") or "g++", "-std=c++17", "-Wall", "-Wextra",
            "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            str(source), "-o", str(binary),
        ], check=True)
        result = subprocess.run([str(binary)], text=True, capture_output=True, check=False)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        logger.info("{}", result.stdout.strip())


if __name__ == "__main__":
    main()
