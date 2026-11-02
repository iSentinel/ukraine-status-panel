#include "marker-store.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace usp_room_recovery_store;

/** Produce a consistent first archive boundary. */
static Marker proposed() {
  Marker marker{};
  marker.cutoff_day = 23000;
  marker.cutoff_hour = marker.cutoff_day * 24U + 12U;
  return marker;
}

/** Loading an optional marker must not create flash state. */
static void absent_readonly() {
  marker_host::reset();
  Marker marker{};
  assert(load(marker) == State::EMPTY);
  assert(marker_host::writable_opens == 0 && marker_host::writes == 0);
  assert(!marker_host::namespace_exists);
}

/** V1 remains byte-compatible, immutable and readable without migration. */
static void v1_compatibility() {
  marker_host::reset();
  Marker wanted = proposed();
  assert(save_once(wanted));
  const auto bytes = marker_host::blob;
  Marker restored{};
  assert(load(restored) == State::READY && same(restored, wanted));
  assert(restored.active_bank == 1 && restored.version == VERSION_V1);
  assert(save_once(wanted) && marker_host::writes == 1);
  wanted.cutoff_hour += 24;
  ++wanted.cutoff_day;
  assert(!save_once(wanted));
  assert(marker_host::blob == bytes && marker_host::writes == 1);
}

/** V1 -> V2 is one record replacement preserving both boundaries. */
static void bank2_selection() {
  marker_host::reset();
  const Marker first = proposed();
  assert(save_once(first));
  const uint32_t day2 = first.cutoff_day - 3U;
  const uint32_t hour2 = day2 * 24U + 7U;
  assert(select_bank2_once(first, hour2, day2));
  Marker restored{};
  assert(load(restored) == State::READY && restored.version == VERSION_V2);
  assert(restored.cutoff_hour == first.cutoff_hour && restored.cutoff_day == first.cutoff_day);
  assert(restored.second_cutoff_hour == hour2 && restored.second_cutoff_day == day2);
  assert(restored.active_bank == 2 && archive_bank(restored) == 1);
  const auto bytes = marker_host::blob;
  assert(select_bank2_once(first, hour2, day2));
  assert(marker_host::blob == bytes && marker_host::writes == 2);
  assert(!select_bank2_once(first, hour2 + 24U, day2 + 1U));
  assert(marker_host::blob == bytes);
}

/** The old reader's exact-size rule rejects v2 and cannot mutate an archive. */
static void old_reader_rejects_v2() {
  marker_host::reset();
  const Marker first = proposed();
  assert(save_once(first));
  assert(select_bank2_once(first, first.cutoff_hour, first.cutoff_day));
  assert(marker_host::blob.size() == sizeof(MarkerV2));
  assert(marker_host::blob.size() != sizeof(LegacyMarker));
}

/** Damaged lengths, checksums and future formats are never absence. */
static void damaged_markers() {
  for (size_t size : {0U, 1U, 19U, 21U, 31U, 33U}) {
    marker_host::reset();
    marker_host::namespace_exists = marker_host::blob_exists = true;
    marker_host::blob.assign(size, 0);
    Marker result{};
    assert(load(result) == State::INVALID);
    assert(!save_once(proposed()) && marker_host::writes == 0);
  }
  marker_host::reset();
  LegacyMarker corrupt = encode_v1(proposed());
  corrupt.checksum ^= 1U;
  const auto *bytes = reinterpret_cast<const uint8_t *>(&corrupt);
  marker_host::blob.assign(bytes, bytes + sizeof(corrupt));
  marker_host::namespace_exists = marker_host::blob_exists = true;
  Marker result{};
  assert(load(result) == State::INVALID);
}

/** Write/commit errors are resolved from durable bytes, not return codes. */
static void uncertain_v1() {
  marker_host::reset();
  marker_host::write_error = ESP_FAIL;
  assert(!save_once(proposed()));
  Marker result{};
  assert(load(result) == State::EMPTY);
  marker_host::reset();
  marker_host::write_error = ESP_FAIL;
  marker_host::write_before_error = true;
  assert(save_once(proposed()));
  assert(load(result) == State::READY);
  marker_host::reset();
  marker_host::commit_error = ESP_FAIL;
  assert(save_once(proposed()));
  assert(load(result) == State::READY);
}

/** V2 uncertain and partial writes either read back exactly or fail closed. */
static void uncertain_v2() {
  const Marker first = proposed();
  marker_host::reset();
  assert(save_once(first));
  marker_host::write_error = ESP_FAIL;
  assert(!select_bank2_once(first, first.cutoff_hour, first.cutoff_day));
  Marker result{};
  assert(load(result) == State::READY && result.version == VERSION_V1);
  marker_host::write_error = 0;
  marker_host::partial_write_bytes = 24;
  assert(!select_bank2_once(first, first.cutoff_hour, first.cutoff_day));
  assert(load(result) == State::INVALID);
  marker_host::reset();
  assert(save_once(first));
  marker_host::write_error = ESP_FAIL;
  marker_host::write_before_error = true;
  assert(select_bank2_once(first, first.cutoff_hour, first.cutoff_day));
  assert(load(result) == State::READY && result.version == VERSION_V2);
}

/** Invalid input and unavailable storage preserve the selection. */
static void rejected_inputs() {
  marker_host::reset();
  Marker invalid = proposed();
  ++invalid.cutoff_day;
  assert(!save_once(invalid) && marker_host::writable_opens == 0);
  marker_host::read_error = ESP_FAIL;
  Marker result{};
  assert(load(result) == State::IO_ERROR);
  assert(!save_once(proposed()) && marker_host::writes == 0);
}

int main() {
  absent_readonly();
  v1_compatibility();
  bank2_selection();
  old_reader_rejects_v2();
  damaged_markers();
  uncertain_v1();
  uncertain_v2();
  rejected_inputs();
  std::cout << "MARKER_STORE_V2=PASS v1_compat bank2 atomic old_reader_reject"
            << " corrupt8 uncertain_v1 uncertain_v2 partial_fail_closed no_device\n";
}
