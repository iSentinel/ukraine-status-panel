#!/usr/bin/env python3
"""Validate the bounded production HTTP 429/Retry-After policy natively."""

from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CXX = shutil.which("g++") or "g++"

CPP = r"""
#include <cassert>
#include <cstdint>
#include <iostream>
#include "usp_http_retry_after.h"

using namespace usp_http_retry_after;

int main() {
  HeaderCapture h;
  h.observe("content-type", "x");
  assert(!h.valid);
  h.observe("retry-after", " 120\t");
  assert(h.valid && h.delay_ms == 120000U);
  h.observe("Retry-After", "60");
  assert(h.delay_ms == 120000U);
  h.observe("RETRY-AFTER", "999999999999999999999999");
  assert(h.delay_ms == 900000U);

  h.reset();
  h.observe("Retry-After", "Wed, 21 Oct 2015 07:28:00 GMT");
  assert(!h.valid);
  h.observe("Retry-After", "1.5");
  assert(!h.valid);
  h.observe("Retry-After", "0");
  assert(h.valid && h.delay_ms == 1000U);
  assert(selected_delay(429, h, 30000U) == 30000U);
  assert(selected_delay(500, h, 30000U) == 30000U);

  Gate gate;
  HeaderCapture provider;
  provider.observe("Retry-After", "120");
  assert(gate.apply(429, provider, 30000U, 1000U) == 120000U);
  assert(!gate.ready(120999U));
  assert(gate.ready(121000U));
  assert(!gate.active);

  HeaderCapture missing;
  assert(gate.apply(429, missing, 15000U, 5000U) == 15000U);
  assert(!gate.ready(19999U));
  assert(gate.ready(20000U));

  Gate wrap;
  HeaderCapture short_delay;
  short_delay.observe("Retry-After", "2");
  constexpr uint32_t start = UINT32_MAX - 500U;
  assert(wrap.apply(429, short_delay, 1000U, start) == 2000U);
  assert(!wrap.ready(1000U));
  assert(wrap.ready(1500U));

  static_assert(sizeof(HeaderCapture) <= 8U);
  static_assert(sizeof(Gate) <= 8U);
  std::cout
      << "HTTP_RETRY_AFTER_NATIVE=PASS delta_only=1 case_insensitive=1 "
         "multi_max=1 malformed_fallback=1 min1s=1 cap15m=1 exact=1 wrap=1 "
         "bytes="
      << sizeof(HeaderCapture) + sizeof(Gate) << "\n";
}
"""


def main() -> None:
    """Compile and run the exact production helper under ASAN/UBSAN."""
    with tempfile.TemporaryDirectory(prefix="usp-http429-") as tmp:
        src = Path(tmp) / "retry.cpp"
        binary = Path(tmp) / "retry"
        src.write_text(CPP)
        subprocess.run(
            [
                CXX,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-O1",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-I",
                str(ROOT / "include"),
                str(src),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
