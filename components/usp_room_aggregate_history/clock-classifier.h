#pragma once
#include <cstdint>
#include <limits>

namespace usp_room_clock_recovery {

/** Observation from an explicit time synchronization, never a sensor sample. */
struct TimeObservation {
  uint32_t epoch;
  uint32_t local_day;
  uint8_t hour;
  bool synchronized;
};

/** Validated bounds restored by the existing aggregate component. */
struct StoredExtent {
  uint32_t raw_cursor;
  uint32_t newest_hour;
  uint32_t newest_day;
  bool have_hour;
  bool have_day;
};

enum class Decision {
  WAIT_SYNCHRONIZATION,
  INVALID_INPUT,
  KEEP_SERIES,
  ARCHIVE_FUTURE,
  LEGACY_AMBIGUOUS,
};

/** A whole bucket difference of thirteen proves a gap greater than one hour. */
inline bool raw_future(const StoredExtent &stored, uint32_t epoch) {
  if (stored.raw_cursor == 0) return false;
  const uint32_t last = stored.raw_cursor - 1U;
  const uint32_t now = epoch / 300U;
  return last > now && last - now > 12U;
}

/** Local hour boundaries support legacy pages without guessing their minutes. */
inline bool calendar_future(const StoredExtent &stored,
                            const TimeObservation &now) {
  const uint32_t hour = now.local_day * 24U + now.hour;
  if (stored.have_hour && stored.newest_hour > hour &&
      stored.newest_hour - hour > 1U) return true;
  if (!stored.have_day || stored.newest_day <= now.local_day) return false;
  return stored.newest_day - now.local_day > 1U || now.hour < 23;
}

/** Classify only; preserving and committing data belongs to another layer. */
inline Decision classify(const StoredExtent &stored,
                         const TimeObservation &now) {
  if (!now.synchronized) return Decision::WAIT_SYNCHRONIZATION;
  constexpr uint32_t maximum = std::numeric_limits<uint32_t>::max();
  if (now.epoch == 0 || now.hour >= 24 ||
      now.local_day > (maximum - 23U) / 24U ||
      stored.raw_cursor > maximum / 300U + 1U)
    return Decision::INVALID_INPUT;
  if (raw_future(stored, now.epoch) || calendar_future(stored, now))
    return Decision::ARCHIVE_FUTURE;
  if (stored.raw_cursor == 0 && !stored.have_hour && stored.have_day &&
      stored.newest_day > now.local_day)
    return Decision::LEGACY_AMBIGUOUS;
  return Decision::KEEP_SERIES;
}

}  // namespace usp_room_clock_recovery
