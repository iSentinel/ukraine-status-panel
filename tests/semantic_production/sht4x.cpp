#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
static float convert(uint16_t raw) {
  uint16_t data[2]{32768, raw};
  // PRODUCTION_EXPRESSIONS
  return humidity;
}
int main() {
  float previous = -1;
  for (uint32_t raw = 0; raw <= 65535; ++raw) {
    const float value = convert(static_cast<uint16_t>(raw));
    const float formula = -6.0f + 125.0f * float(raw) / 65535.0f;
    const float expected = std::min(100.0f, std::max(0.0f, formula));
    assert(std::abs(value - expected) < 0.0001f);
    assert(value >= previous && value >= 0 && value <= 100);
    previous = value;
  }
  std::cout << "SHT4X_PRODUCTION=PASS ticks=65536\n";
}
