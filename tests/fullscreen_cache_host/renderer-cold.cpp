#include "usp_map_cache_renderer.h"
#include "legacy-renderer.h"
#include <cassert>
#include <iostream>
#include <vector>

static constexpr size_t PIXELS = usp_map_cache_data::WIDTH * usp_map_cache_data::HEIGHT;
static constexpr uint16_t GUARD = 0xA55A;

/** Finish bounded steps and check the exact write budget and buffer guards. */
uint64_t finish(usp_map_cache::Renderer &renderer, uint32_t budget,
                std::vector<uint16_t> &pixels) {
  uint64_t written = 0, steps = 0;
  while (!renderer.ready()) {
    renderer.cold_step(budget);
    written += renderer.cold_step_pixels();
    assert(renderer.cold_step_pixels() <= budget);
    assert(pixels.front() == GUARD && pixels.back() == GUARD);
    assert(++steps <= PIXELS * 3 + 100);
  }
  assert(!renderer.cold_pending());
  return written;
}

/** Match first-frame and existing warm/no-op pixels for varied colors/budgets. */
void equivalent(uint32_t budget, unsigned seed) {
  std::vector<uint16_t> expected(PIXELS), actual(PIXELS + 2, GUARD);
  usp_map_cache_legacy::Renderer legacy(expected.data());
  usp_map_cache::Renderer renderer(actual.data() + 1);
  uint16_t colors[27];
  for (unsigned i = 0; i < 27; ++i) colors[i] = (seed * 7919 + i * 1237) & 65535;
  assert(legacy.update(colors) && renderer.cold_begin(colors));
  assert(!renderer.cold_step(0) && renderer.cold_step_pixels() == 0);
  assert(renderer.cold_begin(colors));  // Same request must not disturb progress.
  const auto written = finish(renderer, budget, actual);
  uint64_t expected_writes = PIXELS;
  for (const auto &run : usp_map_cache_data::RUNS) {
    assert(run.length > 0 && run.offset + run.length <= PIXELS);
    expected_writes += run.length;
  }
  assert(written == expected_writes);
  assert(std::equal(expected.begin(), expected.end(), actual.begin() + 1));
  assert(!renderer.update(colors));
  colors[5] ^= 0xFFFF;
  assert(legacy.update(colors) && renderer.update(colors));
  assert(std::equal(expected.begin(), expected.end(), actual.begin() + 1));
  assert(!renderer.cold_begin(colors) && renderer.ready());
}

/** A newer captured request must replace both partially filled and painted data. */
void superseded(bool late) {
  std::vector<uint16_t> expected(PIXELS), actual(PIXELS + 2, GUARD);
  usp_map_cache_legacy::Renderer legacy(expected.data());
  usp_map_cache::Renderer renderer(actual.data() + 1);
  uint16_t colors[27]{};
  renderer.cold_begin(colors);
  const unsigned warmup = late ? (PIXELS / 4096 + 20) : 3;
  for (unsigned i = 0; i < warmup; ++i) renderer.cold_step(4096);
  assert(!renderer.ready());
  for (unsigned i = 0; i < 27; ++i) colors[i] = 997 * (i + 1);
  renderer.cold_begin(colors);
  legacy.update(colors);
  finish(renderer, 4096, actual);
  assert(std::equal(expected.begin(), expected.end(), actual.begin() + 1));
}

int main() {
  for (uint32_t budget : {1U, 3U, 127U, 4096U, 65536U}) equivalent(budget, 1);
  for (unsigned seed = 2; seed < 18; ++seed) equivalent(4096, seed);
  superseded(false);
  superseded(true);
  std::cout << "COLD_RENDERER_NATIVE=PASS exact_pixels guards budget zero supersession warm_noop cases23\n";
}
