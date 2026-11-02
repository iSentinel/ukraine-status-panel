#include "room_history.h"
#include "esp_heap_caps.h"
#include <cassert>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <initializer_list>

using namespace esphome::usp_room_history;
constexpr uint32_t BASE = 1800000000;

// The allocator fixture releases memory after each component lifetime.
using History = RoomHistory;

static void valid_values() {
  room_test_heap::reset();
  History history;
  history.setup();
  assert(history.storage_ok() && history.using_psram());
  assert(history.storage_bytes() == 3456 && history.capacity() == 288);
  for (const float temperature : {-327.68f, -45.0f, 0.0f, 22.25f, 130.0f, 327.67f}) {
    for (const float humidity : {0.0f, 47.5f, 100.0f}) {
      history.clear();
      assert(history.record(BASE, temperature, humidity, true));
      const auto *sample = history.sample(0);
      assert(sample && sample->flags == ROOM_SAMPLE_VALID);
      assert(sample->temperature_centi == std::lround(double(temperature) * 100));
      assert(sample->humidity_centi == std::lround(double(humidity) * 100));
      assert(history.valid_count() == 1 && history.gap_count() == 0);
    }
  }
}

static void require_gap(History &history, float temperature, float humidity,
                        bool valid = true) {
  history.clear();
  std::feclearexcept(FE_ALL_EXCEPT);
  assert(history.record(BASE, temperature, humidity, valid));
  const auto *sample = history.sample(0);
  assert(sample && sample->flags == ROOM_SAMPLE_GAP);
  assert(sample->temperature_centi == 0 && sample->humidity_centi == 0);
  assert(history.valid_count() == 0 && history.gap_count() == 1);
  assert((std::fetestexcept(FE_INVALID) & FE_INVALID) == 0);
}

static void invalid_values() {
  room_test_heap::reset();
  History history;
  history.setup();
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  const float huge = std::numeric_limits<float>::max();
  for (float temperature : {nan, inf, -inf, huge, -huge, 10000.0f,
                            -10000.0f, 327.68f, -327.69f})
    require_gap(history, temperature, 50.0f);
  for (float humidity : {nan, inf, -inf, huge, -1.0f, 100.01f, 200.0f})
    require_gap(history, 22.0f, humidity);
  require_gap(history, 22.0f, 50.0f, false);
  require_gap(history, huge, huge, false);
}

static void replacement_and_time() {
  room_test_heap::reset();
  History history;
  history.setup();
  assert(!history.record(0, 22, 50, true));
  assert(history.record(BASE, 22, 50, true));
  assert(!history.record(BASE - 300, 23, 51, true));
  assert(history.record(BASE + 5, NAN, NAN, false));
  assert(history.count() == 1 && history.gap_count() == 1);
  assert(history.valid_count() == 0);
  assert(history.record(BASE + 10, 23, 51, true));
  assert(history.count() == 1 && history.valid_count() == 1);
  assert(history.gap_count() == 0);
  assert(history.sample(0)->epoch == BASE + 10);
  assert(history.record(BASE + 900, NAN, NAN, false));
  assert(history.count() == 2 && history.sample(1)->flags == ROOM_SAMPLE_GAP);
  assert(history.sample(2) == nullptr);
  history.clear();
  assert(history.count() == 0 && history.sample(0) == nullptr);
}

static void ring_overwrite() {
  room_test_heap::reset();
  History history;
  history.setup();
  for (unsigned index = 0; index < 338; ++index)
    assert(history.record(BASE + index * 300, 22, 50, index % 3 != 0));
  assert(history.count() == 288);
  assert(history.sample(0)->epoch == BASE + 50 * 300);
  assert(history.sample(287)->epoch == BASE + 337 * 300);
  unsigned valid = 0, gaps = 0;
  for (unsigned index = 0; index < 288; ++index) {
    if (history.sample(index)->flags == ROOM_SAMPLE_VALID) ++valid;
    else ++gaps;
  }
  assert(valid == history.valid_count() && gaps == history.gap_count());
  assert(valid + gaps == 288);
}

static void allocation_failures() {
  room_test_heap::reset(room_test_heap::Failure::PSRAM_ONLY);
  {
    History history;
    history.setup();
    assert(history.storage_ok() && !history.using_psram());
    assert(room_test_heap::calls == 2 && !history.warning);
    assert(history.record(BASE, 22, 50, true));
  }
  room_test_heap::reset(room_test_heap::Failure::ALL);
  {
    History history;
    history.setup();
    assert(!history.storage_ok() && history.warning);
    assert(room_test_heap::calls == 2);
    assert(!history.record(BASE, 22, 50, true));
    history.clear();
    assert(history.count() == 0 && history.sample(0) == nullptr);
  }
  room_test_heap::reset();
}

static void probe_conversion() {
  room_test_heap::reset();
  History history;
  history.setup();
  std::feclearexcept(FE_ALL_EXCEPT);
  history.record(BASE, 1.0e30f, 50, true);
  const auto first = *history.sample(0);
  const bool invalid = std::fetestexcept(FE_INVALID) != 0;
  history.record(BASE + 300, 22, 200, true);
  const auto second = *history.sample(1);
  std::printf("{\"huge_temperature_valid\":%u,\"temperature_centi\":%d,"
              "\"floating_invalid\":%u,\"humidity_200_valid\":%u,"
              "\"humidity_centi\":%u}\n",
              unsigned(first.flags == ROOM_SAMPLE_VALID),
              int(first.temperature_centi), unsigned(invalid),
              unsigned(second.flags == ROOM_SAMPLE_VALID),
              unsigned(second.humidity_centi));
}

int main(int argc, char **argv) {
  if (argc == 2 && std::strcmp(argv[1], "--probe") == 0) {
    probe_conversion();
    room_test_heap::release();
    return 0;
  }
  valid_values();
  invalid_values();
  replacement_and_time();
  ring_overwrite();
  allocation_failures();
  room_test_heap::release();
  std::puts("ROOM_HISTORY_NATIVE=PASS actual_component conversion gaps ring allocation");
}
