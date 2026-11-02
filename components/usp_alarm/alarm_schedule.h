#pragma once

#include "usp_clock_presentation.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace usp_alarm_schedule {

constexpr size_t SLOT_COUNT = 3;
constexpr uint32_t STORE_MAGIC = 0x55414C4D;
constexpr uint16_t STORE_VERSION = 1;

/** One civil-time schedule; Sunday is bit zero, matching ESPHome weekday. */
struct Record {
  uint16_t minute{420};
  uint8_t weekdays{127};
  uint8_t enabled{0};
  uint32_t fired_day{0};
};

struct Store {
  uint32_t magic{STORE_MAGIC};
  uint16_t version{STORE_VERSION};
  uint16_t reserved{0};
  std::array<Record, SLOT_COUNT> slots{};
};

static_assert(sizeof(Record) == 8, "Alarm record layout changed");
static_assert(sizeof(Store) == 32, "Alarm storage must stay bounded");

/** The UTC minute detects corrections; the civil day prevents DST repeats. */
struct Clock {
  usp_clock::Calendar local{};
  uint32_t epoch{};
};

inline uint32_t day_key(const usp_clock::Calendar &value) {
  return value.year * 10000U + value.month * 100U + value.day;
}

/** Return the next civil date key without assuming YYYYMMDD + 1 is valid. */
inline uint32_t next_day_key(usp_clock::Calendar value) {
  constexpr uint8_t days[] = {31, 28, 31, 30, 31, 30,
                              31, 31, 30, 31, 30, 31};
  const bool leap = value.year % 4 == 0 &&
      (value.year % 100 != 0 || value.year % 400 == 0);
  const unsigned maximum = days[value.month - 1] +
      (value.month == 2 && leap ? 1 : 0);
  if (value.day < maximum) {
    ++value.day;
  } else {
    value.day = 1;
    if (value.month < 12) ++value.month;
    else { value.month = 1; ++value.year; }
  }
  return day_key(value);
}

/** Keep one-day rollback dedup.
 * Ignore implausibly future persisted firing days.
 */
inline bool firing_guard_active(uint32_t fired_day,
                                const usp_clock::Calendar &local) {
  const uint32_t today = day_key(local);
  if (fired_day < today) return false;
  if (fired_day == today) return true;
  return fired_day <= next_day_key(local);
}

/** Accept only an exact, unambiguous 24-hour HH:MM value. */
inline bool parse_time(const char *text, uint16_t *minute) {
  if (text == nullptr || minute == nullptr || std::strlen(text) != 5 ||
      text[2] != ':' || text[0] < '0' || text[0] > '9' ||
      text[1] < '0' || text[1] > '9' || text[3] < '0' ||
      text[3] > '9' || text[4] < '0' || text[4] > '9') return false;
  const unsigned hours = (text[0] - '0') * 10 + text[1] - '0';
  const unsigned minutes = (text[3] - '0') * 10 + text[4] - '0';
  if (hours > 23 || minutes > 59) return false;
  *minute = hours * 60 + minutes;
  return true;
}

inline bool valid_day(uint32_t value) {
  if (value == 0) return true;
  if (value < 20190101U || value > 20991231U) return false;
  const usp_clock::Calendar calendar{
      static_cast<uint16_t>(value / 10000),
      static_cast<uint8_t>(value / 100 % 100),
      static_cast<uint8_t>(value % 100), 0, 0, 1, true};
  return usp_clock::valid(calendar);
}

inline bool valid(const Record &record) {
  return record.minute < 1440 && record.weekdays > 0 &&
      record.weekdays <= 127 && record.enabled <= 1 &&
      valid_day(record.fired_day);
}

/** Detect only the one-hour civil jump used by spring DST.
 * UTC continuity is checked separately by poll(), so boot/SNTP jumps never catch up.
 */
inline bool spring_forward_gap(const usp_clock::Calendar &previous,
                               const usp_clock::Calendar &current) {
  if (previous.year != current.year || previous.month != current.month ||
      previous.day != current.day || previous.weekday != current.weekday) {
    return false;
  }
  const unsigned before = previous.hour * 60U + previous.minute;
  const unsigned now = current.hour * 60U + current.minute;
  return now == before + 61U;
}

/** No heap allocation, network work, sound output or flash writes in this core. */
class Schedule {
 public:
  const Store &store() const { return store_; }
  const Record *slot(size_t index) const {
    return index < SLOT_COUNT ? &store_.slots[index] : nullptr;
  }
  uint8_t active() const { return active_; }
  uint16_t active_minute(size_t index) const { return active_minutes_[index]; }
  void dismiss() { active_ = 0; }

  /** Corrupt or unsupported storage leaves all default schedules disabled. */
  bool restore(const Store &value) {
    if (value.magic != STORE_MAGIC || value.version != STORE_VERSION ||
        value.reserved != 0) return false;
    for (const auto &record : value.slots)
      if (!valid(record)) return false;
    store_ = value;
    armed_ = false;
    active_ = 0;
    return true;
  }

  /** Preserve the firing guard when a user edits or toggles a schedule. */
  bool configure(size_t index, uint16_t minute, uint8_t days, bool enabled) {
    if (index >= SLOT_COUNT) return false;
    const Record next{minute, days, static_cast<uint8_t>(enabled),
                      store_.slots[index].fired_day};
    if (!valid(next)) return false;
    store_.slots[index] = next;
    if (!enabled) active_ &= static_cast<uint8_t>(~(1U << index));
    return true;
  }

  /** Skip boot, regained trust and UTC jumps; catch only a one-hour DST gap. */
  uint8_t poll(const Clock &clock) {
    if (!usp_clock::valid(clock.local) || clock.epoch < 1546300800U) {
      armed_ = false;
      return 0;
    }
    const uint32_t utc_minute = clock.epoch / 60;
    const int64_t delta = int64_t(utc_minute) - last_utc_minute_;
    const auto previous_local = last_local_;
    last_utc_minute_ = utc_minute;
    last_local_ = clock.local;
    if (!armed_) { armed_ = true; return 0; }
    if (delta != 1) return 0;
    const unsigned current_minute =
        clock.local.hour * 60U + clock.local.minute;
    const unsigned previous_minute =
        previous_local.hour * 60U + previous_local.minute;
    const bool spring_gap = spring_forward_gap(previous_local, clock.local);
    uint8_t due = 0;
    const uint32_t day = day_key(clock.local);
    for (size_t index = 0; index < SLOT_COUNT; ++index) {
      auto &record = store_.slots[index];
      const bool already_fired =
          firing_guard_active(record.fired_day, clock.local);
      const bool weekday_due =
          record.weekdays & (1U << (clock.local.weekday - 1));
      const bool minute_due = record.minute == current_minute;
      const bool skipped_due = spring_gap && record.minute > previous_minute &&
                               record.minute < current_minute;
      if (!record.enabled || already_fired || !weekday_due ||
          (!minute_due && !skipped_due)) {
        continue;
      }
      record.fired_day = day;
      active_minutes_[index] = record.minute;
      due |= 1U << index;
    }
    active_ |= due;
    return due;
  }

 private:
  Store store_{};
  uint32_t last_utc_minute_{};
  usp_clock::Calendar last_local_{};
  bool armed_{false};
  uint8_t active_{};
  std::array<uint16_t, SLOT_COUNT> active_minutes_{};
};

}  // namespace usp_alarm_schedule
