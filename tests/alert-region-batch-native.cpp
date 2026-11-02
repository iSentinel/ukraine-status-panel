#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

#include "usp_alert_region_batch.h"

static uint32_t legacy_color(uint8_t state, uint8_t palette) {
  uint32_t color = 0x34424E;
  if (state == 1) color = 0x1F8A4C;
  else if (state == 2 || state == 8) color = 0xF5BE26;
  else if (state == 16) color = 0xF97316;
  else if (state == 4) color = 0xDC3545;
  else if (state == 32) color = 0x9C27B0;
  else if (state == 64) color = 0xA855F7;
  else if (state == 128) color = 0xF97316;
  return usp_alert_palette::color_hex(color, palette);
}

// Verify every supported color against the previous mapping.
static void check_colors() {
  constexpr std::array<uint8_t, 9> states{0, 1, 2, 4, 8, 16, 32, 64, 128};
  for (uint8_t palette = 0; palette < 3; ++palette) {
    for (uint8_t state : states) {
      assert(usp_alert_region_colors::color_hex(state, palette) ==
             legacy_color(state, palette));
    }
  }

}

// Exercise dirty, clean and unsupported entries using one shared snapshot.
struct Fixture {
  std::array<uint8_t, 25> desired{};
  std::array<uint8_t, 25> cache{};
  uint32_t valid = 0;
  uint32_t inflight = (1UL << 20);
  uint8_t changed = 3;
};

static void check_dirty(Fixture &f) {
  auto &desired = f.desired;
  auto &cache = f.cache;
  auto &valid = f.valid;
  auto &inflight = f.inflight;
  auto &changed = f.changed;
  constexpr std::array<uint8_t, 5> states{0, 1, 2, 4, 8};
  for (size_t i = 5; i < 10; ++i) desired[i] = states[i - 5];
  uint32_t dirty = 0;
  for (size_t i = 5; i < 10; ++i) dirty |= 1UL << i;
  size_t calls = 0;
  std::array<uint32_t, 5> colors{};
  usp_alert_region_batch::render(
      5, 5, desired.data(), dirty, cache.data(), valid, inflight, changed, 0,
      [&](size_t offset, uint32_t color) {
        assert(offset < colors.size());
        colors[offset] = color;
        ++calls;
      });
  assert(calls == 5);
  assert(changed == 8);
  assert((valid & dirty) == dirty);
  assert((inflight & dirty) == 0);
  assert((inflight & (1UL << 20)) != 0);
  for (size_t i = 5; i < 10; ++i) {
    assert(cache[i] == desired[i]);
    assert(colors[i - 5] == legacy_color(desired[i], 0));
  }

}

// A clean entry keeps cached data but clears its inflight bit.
static void check_clean(Fixture &f) {
  auto &desired = f.desired;
  auto &cache = f.cache;
  auto &valid = f.valid;
  auto &inflight = f.inflight;
  auto &changed = f.changed;
  desired[11] = 16;
  cache[11] = 91;
  const uint32_t clean_bit = 1UL << 11;
  inflight |= clean_bit;
  const uint32_t clean_valid_before = valid;
  const uint8_t clean_changed_before = changed;
  usp_alert_region_batch::render(
      11, 1, desired.data(), 0, cache.data(), valid, inflight, changed, 0,
      [&](size_t, uint32_t) { assert(false); });
  assert(cache[11] == 91);
  assert(valid == clean_valid_before);
  assert(changed == clean_changed_before);
  assert((inflight & clean_bit) == 0);

}

// Unsupported states preserve the previous visible state.
static void check_unsupported(Fixture &f) {
  auto &desired = f.desired;
  auto &cache = f.cache;
  auto &valid = f.valid;
  auto &inflight = f.inflight;
  auto &changed = f.changed;
  desired[10] = 3;
  cache[10] = 77;
  const uint32_t dirty = 1UL << 10;
  inflight |= dirty;
  const uint32_t valid_before = valid;
  const uint8_t changed_before = changed;
  usp_alert_region_batch::render(
      10, 1, desired.data(), dirty, cache.data(), valid, inflight, changed, 0,
      [&](size_t, uint32_t) { assert(false); });
  assert(cache[10] == 77);
  assert(valid == valid_before);
  assert(changed == changed_before);
  assert((inflight & dirty) == 0);

}

int main() {
  check_colors();
  Fixture fixture;
  check_dirty(fixture);
  check_clean(fixture);
  check_unsupported(fixture);
  std::puts("ALERT_REGION_BATCH_NATIVE=PASS colors=27 batch5 bookkeeping unsupported");
  return 0;
}
