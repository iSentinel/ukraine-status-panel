#pragma once

#include "nvs.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace usp_room_recovery_store {

constexpr const char *NAMESPACE = "usp_room_hist";
constexpr const char *KEY = "active";
constexpr uint32_t MAGIC = 0x55535037U;
constexpr uint32_t VERSION_V1 = 1U;
constexpr uint32_t VERSION_V2 = 2U;

/** Original on-flash format. Keep byte-exact for compatibility. */
struct LegacyMarker {
  uint32_t magic{MAGIC};
  uint32_t version{VERSION_V1};
  uint32_t cutoff_hour{0};
  uint32_t cutoff_day{0};
  uint32_t checksum{0};
};

/** Second archive selection. A larger record makes old code fail closed. */
struct MarkerV2 {
  uint32_t magic{MAGIC};
  uint32_t version{VERSION_V2};
  uint32_t cutoff_hour{0};
  uint32_t cutoff_day{0};
  uint32_t second_cutoff_hour{0};
  uint32_t second_cutoff_day{0};
  uint32_t active_bank{2};
  uint32_t checksum{0};
};

/** Normalized marker used by recovery code for either persistent version. */
struct Marker {
  uint32_t magic{MAGIC};
  uint32_t version{VERSION_V1};
  uint32_t cutoff_hour{0};
  uint32_t cutoff_day{0};
  uint32_t second_cutoff_hour{0};
  uint32_t second_cutoff_day{0};
  uint32_t active_bank{1};
};

static_assert(sizeof(LegacyMarker) == 20, "Marker v1 layout changed");
static_assert(sizeof(MarkerV2) == 32, "Marker v2 layout changed");

enum class State { EMPTY, READY, INVALID, IO_ERROR };

/** FNV-1a over a record with its checksum field logically zero. */
template<typename Record> uint32_t checksum_record(Record record) {
  record.checksum = 0;
  const auto *bytes = reinterpret_cast<const uint8_t *>(&record);
  uint32_t hash = 2166136261U;
  for (size_t index = 0; index < sizeof(record); ++index) {
    hash ^= bytes[index];
    hash *= 16777619U;
  }
  return hash;
}

inline bool valid_cutoff(uint32_t hour, uint32_t day) {
  return hour / 24U == day;
}

inline bool valid(const Marker &marker) {
  if (marker.magic != MAGIC || !valid_cutoff(marker.cutoff_hour, marker.cutoff_day))
    return false;
  if (marker.version == VERSION_V1)
    return marker.active_bank == 1U && marker.second_cutoff_hour == 0U &&
           marker.second_cutoff_day == 0U;
  return marker.version == VERSION_V2 && marker.active_bank == 2U &&
         valid_cutoff(marker.second_cutoff_hour, marker.second_cutoff_day);
}

inline bool same(const Marker &left, const Marker &right) {
  return left.magic == right.magic && left.version == right.version &&
         left.cutoff_hour == right.cutoff_hour && left.cutoff_day == right.cutoff_day &&
         left.second_cutoff_hour == right.second_cutoff_hour &&
         left.second_cutoff_day == right.second_cutoff_day &&
         left.active_bank == right.active_bank;
}

inline uint32_t active_bank(const Marker &marker) {
  return marker.version == VERSION_V2 ? 2U : 1U;
}

inline uint32_t archive_bank(const Marker &marker) {
  return active_bank(marker) - 1U;
}

inline uint32_t selected_cutoff_hour(const Marker &marker) {
  return marker.version == VERSION_V2 ? marker.second_cutoff_hour : marker.cutoff_hour;
}

inline uint32_t selected_cutoff_day(const Marker &marker) {
  return marker.version == VERSION_V2 ? marker.second_cutoff_day : marker.cutoff_day;
}

inline bool decode(const LegacyMarker &stored, Marker &marker) {
  if (stored.magic != MAGIC || stored.version != VERSION_V1 ||
      stored.checksum != checksum_record(stored)) return false;
  marker = {stored.magic, stored.version, stored.cutoff_hour, stored.cutoff_day, 0, 0, 1};
  return valid(marker);
}

inline bool decode(const MarkerV2 &stored, Marker &marker) {
  if (stored.magic != MAGIC || stored.version != VERSION_V2 ||
      stored.checksum != checksum_record(stored)) return false;
  marker = {stored.magic, stored.version, stored.cutoff_hour, stored.cutoff_day,
            stored.second_cutoff_hour, stored.second_cutoff_day, stored.active_bank};
  return valid(marker);
}

inline LegacyMarker encode_v1(const Marker &marker) {
  LegacyMarker stored{marker.magic, VERSION_V1, marker.cutoff_hour, marker.cutoff_day, 0};
  stored.checksum = checksum_record(stored);
  return stored;
}

inline MarkerV2 encode_v2(const Marker &marker) {
  MarkerV2 stored{marker.magic, VERSION_V2, marker.cutoff_hour, marker.cutoff_day,
                  marker.second_cutoff_hour, marker.second_cutoff_day, 2U, 0};
  stored.checksum = checksum_record(stored);
  return stored;
}

/** Read the exact persistent record without ever creating the namespace. */
inline State load(Marker &marker) {
  nvs_handle_t handle{};
  const esp_err_t opened = nvs_open(NAMESPACE, NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return State::EMPTY;
  if (opened != ESP_OK) return State::IO_ERROR;
  size_t size = 0;
  const esp_err_t sized = nvs_get_blob(handle, KEY, nullptr, &size);
  if (sized == ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); return State::EMPTY; }
  if (sized != ESP_OK) { nvs_close(handle); return State::IO_ERROR; }
  bool decoded = false;
  esp_err_t read = ESP_OK;
  if (size == sizeof(LegacyMarker)) {
    LegacyMarker stored{}; read = nvs_get_blob(handle, KEY, &stored, &size);
    if (read == ESP_OK) decoded = decode(stored, marker);
  } else if (size == sizeof(MarkerV2)) {
    MarkerV2 stored{}; read = nvs_get_blob(handle, KEY, &stored, &size);
    if (read == ESP_OK) decoded = decode(stored, marker);
  }
  nvs_close(handle);
  if (read != ESP_OK) return State::IO_ERROR;
  return decoded ? State::READY : State::INVALID;
}

/** Persist one record and decide success only from an exact readback. */
template<typename Record>
bool write_and_verify(const Record &record, const Marker &expected) {
  nvs_handle_t handle{};
  if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  (void) nvs_set_blob(handle, KEY, &record, sizeof(record));
  (void) nvs_commit(handle);
  nvs_close(handle);
  Marker actual{};
  return load(actual) == State::READY && same(actual, expected);
}

/** Select bank1 once. Existing v1 bytes remain immutable. */
inline bool save_once(const Marker &wanted) {
  if (!valid(wanted) || wanted.version != VERSION_V1) return false;
  Marker stored{};
  const State state = load(stored);
  if (state == State::READY) return same(stored, wanted);
  if (state != State::EMPTY) return false;
  return write_and_verify(encode_v1(wanted), wanted);
}

/** Atomically advance an exact v1 selection to bank2 and preserve both cutoffs. */
inline bool select_bank2_once(const Marker &current, uint32_t cutoff_hour,
                              uint32_t cutoff_day) {
  if (!valid(current) || current.version != VERSION_V1 ||
      !valid_cutoff(cutoff_hour, cutoff_day)) return false;
  Marker wanted = current;
  wanted.version = VERSION_V2;
  wanted.second_cutoff_hour = cutoff_hour;
  wanted.second_cutoff_day = cutoff_day;
  wanted.active_bank = 2U;
  Marker stored{};
  const State state = load(stored);
  if (state != State::READY) return false;
  if (same(stored, wanted)) return true;
  if (!same(stored, current)) return false;
  return write_and_verify(encode_v2(wanted), wanted);
}

}  // namespace usp_room_recovery_store
