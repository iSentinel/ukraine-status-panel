#include "room_history.h"
#include "esp_heap_caps.h"
#include <cstdio>
#include <initializer_list>
using namespace esphome::usp_room_history;
constexpr uint32_t START = 1800000000U;

/** Release the fixture allocation on every return path. */
struct AllocationGuard {
  ~AllocationGuard() { room_test_heap::release(); }
};

/** An older finite value or gap must not replace a newer accepted sample. */
int earlier(bool valid) {
  room_test_heap::reset();
  AllocationGuard cleanup;
  RoomHistory history;
  history.setup();
  if (!history.record(START + 250U, 20.0f, 50.0f, true)) return 33;
  const bool accepted = history.record(START + 240U, 21.0f, 60.0f, valid);
  const auto *value = history.sample(0);
  const bool intact = value && value->epoch == START + 250U
      && value->temperature_centi == 2000 && value->humidity_centi == 5000
      && value->flags == ROOM_SAMPLE_VALID && history.count() == 1U
      && history.valid_count() == 1U && history.gap_count() == 0U;
  std::printf("OLDER_INPUT valid=%u accepted=%u retained_newest=%u\n",
              unsigned(valid), unsigned(accepted), unsigned(intact));
  return !accepted && intact ? 0 : 31;
}

/** Preserve equal-time refreshes and newer readings in either interval. */
int equal_and_newer() {
  room_test_heap::reset();
  AllocationGuard cleanup;
  RoomHistory history;
  history.setup();
  if (!history.record(START + 250U, 20.0f, 50.0f, true)) return 33;
  if (!history.record(START + 250U, 27.0f, 55.0f, true)) return 32;
  if (history.sample(0)->temperature_centi != 2700) return 32;
  if (!history.record(START + 251U, 28.0f, 56.0f, true)) return 33;
  if (!history.record(START + 300U, 29.0f, 57.0f, true)) return 33;
  if (history.count() != 2U || history.sample(1)->epoch != START + 300U) return 33;
  return 0;
}

/** Check all relative offsets for five starting times and both value states. */
int dense_order() {
  room_test_heap::reset();
  AllocationGuard cleanup;
  RoomHistory history;
  history.setup();
  unsigned cases = 0;
  for (uint32_t latest : {0U, 1U, 149U, 298U, 299U}) {
    for (uint32_t incoming = 0; incoming < 300U; ++incoming) {
      for (bool valid : {false, true}) {
        history.clear();
        if (!history.record(START + latest, 20.0f, 50.0f, true)) return 34;
        const bool accepted = history.record(START + incoming, 21.0f, 60.0f, valid);
        if (accepted != (incoming >= latest)) return 34;
        const auto *value = history.sample(0);
        const bool gap = accepted && !valid;
        const uint32_t expected_epoch = START + (accepted ? incoming : latest);
        if (!value || value->epoch != expected_epoch || history.count() != 1U) return 34;
        if (value->flags != (gap ? ROOM_SAMPLE_GAP : ROOM_SAMPLE_VALID)) return 34;
        if (history.valid_count() != unsigned(!gap) || history.gap_count() != unsigned(gap)) return 34;
        ++cases;
      }
    }
  }
  history.clear();
  for (unsigned i = 0; i < 338U; ++i)
    if (!history.record(START + i * 300U + 250U, 20.0f, 50.0f, true)) return 35;
  if (history.record(START + 337U * 300U + 249U, 21.0f, 51.0f, true)) return 35;
  if (history.count() != 288U || history.sample(287)->epoch != START + 337U * 300U + 250U) return 35;
  std::printf("DENSE_TIME_CASES=%u ring_wrap=PASS\n", cases);
  return 0;
}

int main() {
  const int a = earlier(true);
  const int b = earlier(false);
  const int c = equal_and_newer();
  if (a || b) return 31;
  if (c) return c;
  const int d = dense_order();
  if (d) return d;
  std::puts("ROOM_SAME_BUCKET_TIME_ORDER=PASS older_valid older_gap equal newer");
}
