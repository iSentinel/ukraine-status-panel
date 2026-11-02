#!/usr/bin/env python3
"""Compile and validate Room period captions and fallback semantics."""

from pathlib import Path
import subprocess
import tempfile

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]

SOURCE = r'''
#include <cassert>
#include <cstring>
#include <iostream>
#include "usp_room_period_text.h"

int main() {
  using usp_room_period::Period;
  struct Case {
    Period period;
    const char *button;
    const char *history[3];
    const char *left[3];
  };
  const Case cases[] = {
    {Period::HOURS_24, "24H", {"Історія 24 год", "История 24 ч", "24h history"}, {"-24 год", "-24 ч", "-24 h"}},
    {Period::DAYS_7, "7D", {"Історія 7 днів", "История 7 дней", "7-day history"}, {"-7 днів", "-7 дней", "-7 d"}},
    {Period::DAYS_30, "30D", {"Історія 30 днів", "История 30 дней", "30-day history"}, {"-30 днів", "-30 дней", "-30 d"}},
    {Period::YEAR_1, "1Y", {"Історія 1 року", "История 1 года", "1-year history"}, {"-1 рік", "-1 год", "-1 y"}},
  };
  unsigned checked = 0;
  for (const auto &item : cases) {
    assert(std::strcmp(usp_room_period_text::button(item.period), item.button) == 0);
    for (unsigned language = 0; language < 256; ++language) {
      const unsigned li = language < 2 ? language : 2;
      const auto &view = usp_room_period_text::view(language, item.period);
      assert(std::strcmp(view.history, item.history[li]) == 0);
      assert(std::strcmp(view.left, item.left[li]) == 0);
      assert(view.note[0] != '\0');
      assert(view.temperature[0] != '\0');
      assert(view.humidity[0] != '\0');
      ++checked;
    }
  }
  assert(usp_room_period::sanitize(4) == Period::HOURS_24);
  assert(usp_room_period::sanitize(255) == Period::HOURS_24);
  const auto &fallback = usp_room_period_text::view(255, static_cast<Period>(255));
  assert(std::strcmp(fallback.history, "24h history") == 0);
  std::cout << "ROOM_PERIOD_TEXT_NATIVE=PASS checked=" << checked
            << " invalid_period_fallback=2 language_fallback=1\n";
}
'''


def main() -> None:
    """Compile with sanitizers and execute the exact production text header."""
    with tempfile.TemporaryDirectory(prefix="usp-room-period-text-") as temp:
        directory = Path(temp)
        source = directory / "test.cpp"
        binary = directory / "test"
        source.write_text(SOURCE, encoding="utf-8")
        command = [
            "g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(ROOT / "include"), str(source), "-o", str(binary),
        ]
        subprocess.run(command, check=True, cwd=ROOT)
        result = subprocess.run([str(binary)], check=True, cwd=ROOT,
                                capture_output=True, text=True)
        logger.info("{}", result.stdout.strip())


if __name__ == "__main__":
    main()
