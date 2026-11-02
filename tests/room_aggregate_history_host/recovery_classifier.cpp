#include "clock-classifier.h"
#include <cassert>
#include <iostream>

using namespace usp_room_clock_recovery;
constexpr uint32_t EPOCH = 1793095200U;
constexpr uint32_t DAY = 23000U;

/** Old samples cannot request recovery without a fresh synchronization. */
static unsigned raw_boundaries() {
  const TimeObservation now{EPOCH, DAY, 12, true};
  const TimeObservation unconfirmed{EPOCH, DAY, 12, false};
  for (int delta = -20; delta <= 20; ++delta) {
    const uint32_t cursor = EPOCH / 300U + delta + 1U;
    const StoredExtent stored{cursor, 0, 0, false, false};
    const Decision expected = delta > 12 ? Decision::ARCHIVE_FUTURE
                                         : Decision::KEEP_SERIES;
    assert(classify(stored, now) == expected);
    assert(classify(stored, unconfirmed) == Decision::WAIT_SYNCHRONIZATION);
  }
  return 82;
}

/** A legacy hour must prove a gap exceeding one hour before recovery. */
static unsigned calendar_boundaries() {
  for (uint8_t hour = 0; hour < 24; ++hour) {
    for (uint32_t delta = 0; delta < 5; ++delta) {
      const TimeObservation now{EPOCH, DAY, hour, true};
      const StoredExtent stored{0, DAY * 24U + hour + delta, 0, true, false};
      const Decision expected = delta > 1 ? Decision::ARCHIVE_FUTURE
                                          : Decision::KEEP_SERIES;
      assert(classify(stored, now) == expected);
    }
  }
  return 120;
}

/** Do not invent the missing hour in a daily-only legacy page. */
static unsigned daily_boundaries() {
  for (uint8_t hour = 0; hour < 24; ++hour) {
    for (uint32_t delta = 0; delta < 3; ++delta) {
      const TimeObservation now{EPOCH, DAY, hour, true};
      const StoredExtent stored{0, 0, DAY + delta, false, true};
      Decision expected = Decision::KEEP_SERIES;
      if (delta > 0) expected = delta > 1 || hour < 23
          ? Decision::ARCHIVE_FUTURE : Decision::LEGACY_AMBIGUOUS;
      assert(classify(stored, now) == expected);
    }
  }
  return 72;
}

/** Preserve DST behavior and reject malformed state before any data action. */
static unsigned special_cases() {
  TimeObservation now{EPOCH, DAY, 2, true};
  StoredExtent stored{EPOCH / 300U + 1U, DAY * 24U + 3U, DAY, true, true};
  assert(classify(stored, now) == Decision::KEEP_SERIES);
  now.hour = 24;
  assert(classify(stored, now) == Decision::INVALID_INPUT);
  now.hour = 2;
  now.epoch = 0;
  assert(classify(stored, now) == Decision::INVALID_INPUT);
  now.epoch = EPOCH;
  stored.raw_cursor = UINT32_MAX;
  assert(classify(stored, now) == Decision::INVALID_INPUT);
  stored.raw_cursor = 0;
  now.local_day = UINT32_MAX;
  assert(classify(stored, now) == Decision::INVALID_INPUT);
  return 5;
}

int main() {
  const unsigned cases = raw_boundaries() + calendar_boundaries() +
                         daily_boundaries() + special_cases();
  assert(cases == 279);
  std::cout << "CLOCK_CLASSIFIER=PASS cases=" << cases
            << " input_only no_storage no_device\n";
}
