#include "room_aggregate_history_host/recovery_test_support.h"

using SavedPages = decltype(esphome::host_preferences.persisted);
using usp_room_recovery_store::Marker;
using usp_room_recovery_store::MarkerV2;
using usp_room_recovery_store::State;
using usp_room_recovery_store::VERSION_V2;
using usp_room_recovery_store::load;
constexpr uint32_t BASE = 1793088000U;

/** Start independent cases without losing stored bytes during simulated reboot. */
static void reset_n7() {
  reset_host();
  esphome::host_preferences.make_calls = 0;
  esphome::host_preferences.fail_sync_after = -1;
  esphome::host_preferences.pending_order.clear();
}

/** Original pages include three correct samples and one later bad-clock sample. */
static void seed_mixed(RoomAggregateHistory &history) {
  assert(history.record(BASE, 2026, 300, 10, 20, 50, true));
  assert(history.record(BASE + 300, 2026, 300, 10, 22, 52, true));
  assert(history.record(BASE + 3600, 2026, 300, 11, 24, 54, true));
  assert(history.record(BASE + 8 * 3600, 2026, 300, 18, 99, 99, true));
}

/** The archived keys must remain byte-identical after choosing bank1. */
static void unchanged_archive(const SavedPages &saved) {
  for (const auto &item : saved) {
    const auto found = esphome::host_preferences.persisted.find(item.first);
    assert(found != esphome::host_preferences.persisted.end());
    assert(found->second == item.second);
  }
}

/** Reconstructed totals must contain real retained samples, without the future. */
static void correct_prefix(const RoomAggregateHistory &history) {
  assert(history.hourly_count() == 2 && history.daily_count() == 1);
  assert(history.hourly_sample(0)->valid_count == 2);
  assert(history.hourly_sample(0)->temperature_sum_centi == 4200);
  assert(history.hourly_sample(1)->valid_count == 1);
  const auto *day = last_day(history);
  assert(day->valid_count == 3 && day->gap_count == 0);
  assert(day->temperature_sum_centi == 6600);
  assert(day->temperature_min_centi == 2000 && day->temperature_max_centi == 2400);
}

/** No sensor and no confirmed correction means no new persistent state. */
static void test_dormant_no_authority() {
  reset_n7();
  RoomAggregateHistory history;
  history.setup();
  assert(!history.active() && host_live_bytes == 0);
  assert(!history.confirm_time(BASE, 2026, 300, 10, false));
  assert(history.confirm_time(BASE, 2026, 300, 10, true));
  assert(marker_host::writable_opens == 0 && marker_host::writes == 0);
  assert(esphome::host_preferences.save_calls == 0);
}

/** Preserve good hours, rebuild the mixed day and leave source pages unchanged. */
static void test_prefix_and_marker_only_reboot() {
  reset_n7();
  SavedPages archived;
  {
    RoomAggregateHistory history;
    history.setup();
    seed_mixed(history);
    assert(history.flush());
    archived = esphome::host_preferences.persisted;
    assert(last_day(history)->valid_count == 4);
    assert(!history.confirm_time(BASE + 7200, 2026, 300, 12, false));
    assert(history.confirm_time(BASE + 7200, 2026, 300, 12, true));
    assert(history.recovery_active() && !history.recovery_blocked());
    correct_prefix(history);
    unchanged_archive(archived);
  }
  RoomAggregateHistory restored;
  restored.setup();
  assert(restored.recovery_active() && !restored.recovery_blocked());
  correct_prefix(restored);
  unchanged_archive(archived);
}

/** Current pages append after the frozen partial intervals and keep N6. */
static void test_current_series_reboot() {
  reset_n7();
  SavedPages archived;
  {
    RoomAggregateHistory history;
    history.setup();
    seed_mixed(history);
    assert(history.flush());
    archived = esphome::host_preferences.persisted;
    assert(history.confirm_time(BASE + 7200, 2026, 300, 12, true));
    assert(history.record(BASE + 7200, 2026, 300, 12, 25, 55, true));
    assert(history.flush());
    assert(history.hourly_count() == 3 && last_day(history)->valid_count == 4);
    assert(history.record(BASE + 14 * 3600, 2026, 301, 0, 26, 56, true));
    assert(history.flush());
  }
  RoomAggregateHistory restored;
  restored.setup();
  assert(restored.hourly_count() == 4 && restored.daily_count() == 2);
  assert(restored.daily_sample(0)->valid_count == 4);
  assert(last_day(restored)->valid_count == 1);
  assert(!restored.record(BASE + 14 * 3600 + 60, 2026, 301, 0, 30, 60, true));
  assert(restored.flush());
  unchanged_archive(archived);
}

/** A partial source sync cannot commit a recovery marker ahead of its data. */
static void test_partial_sync_retry() {
  reset_n7();
  RoomAggregateHistory history;
  history.setup();
  seed_mixed(history);
  esphome::host_preferences.fail_sync_after = 1;
  assert(!history.confirm_time(BASE + 7200, 2026, 300, 12, true));
  assert(!history.recovery_active() && marker_host::writes == 0);
  assert(esphome::host_preferences.pending.empty());
  esphome::host_preferences.fail_sync_after = -1;
  assert(history.confirm_time(BASE + 7200, 2026, 300, 12, true));
  correct_prefix(history);
}

/** Do not modify the archive while marker persistence remains uncertain. */
static void test_uncertain_marker_readback() {
  reset_n7();
  RoomAggregateHistory history;
  history.setup();
  seed_mixed(history);
  assert(history.flush());
  const auto archived = esphome::host_preferences.persisted;
  marker_host::read_error_after_write = true;
  assert(!history.confirm_time(BASE + 7200, 2026, 300, 12, true));
  assert(history.recovery_blocked() && marker_host::blob_exists);
  assert(!history.record(BASE + 9 * 3600, 2026, 300, 19, 40, 60, true));
  history.on_shutdown();
  unchanged_archive(archived);
  marker_host::read_error = 0;
  marker_host::read_error_after_write = false;
  assert(history.confirm_time(BASE + 7200, 2026, 300, 12, true));
  assert(marker_host::writes == 1);
  correct_prefix(history);
}

/** A marker write rejected before persistence can be retried without data loss. */
static void test_marker_write_retry() {
  reset_n7();
  RoomAggregateHistory history;
  history.setup();
  seed_mixed(history);
  assert(history.flush());
  marker_host::write_error = ESP_FAIL;
  assert(!history.confirm_time(BASE + 7200, 2026, 300, 12, true));
  assert(history.recovery_blocked() && !history.recovery_active());
  marker_host::write_error = 0;
  assert(history.confirm_time(BASE + 7200, 2026, 300, 12, true));
  const auto writes = marker_host::writes;
  const auto saves = esphome::host_preferences.save_calls;
  assert(history.confirm_time(BASE + 7500, 2026, 300, 12, true));
  assert(marker_host::writes == writes && esphome::host_preferences.save_calls == saves);
  correct_prefix(history);
}

/** Correct-date archive reads find past pages hidden by a distant future page. */
static void test_distant_future_window() {
  reset_n7();
  SavedPages archived;
  {
    RoomAggregateHistory history;
    history.setup();
    assert(history.record(BASE, 2026, 300, 10, 20, 50, true));
    assert(history.record(BASE + 365U * 86400U, 2027, 300, 10, 99, 99, true));
    assert(history.flush());
    archived = esphome::host_preferences.persisted;
  }
  RoomAggregateHistory restored;
  restored.setup();
  assert(restored.hourly_count() == 1 && last_day(restored)->temperature_sum_centi == 9900);
  assert(restored.confirm_time(BASE + 7200, 2026, 300, 12, true));
  assert(restored.hourly_count() == 1 && last_day(restored)->valid_count == 1);
  assert(last_day(restored)->temperature_sum_centi == 2000);
  unchanged_archive(archived);
}

/** Legacy future pages stay byte-identical while a new earlier series starts. */
static void test_legacy_future(const std::string &fixture) {
  load_fixture(fixture);
  esphome::host_preferences.fail_sync_after = -1;
  const auto archived = esphome::host_preferences.persisted;
  RoomAggregateHistory history;
  history.setup();
  const uint32_t now = BASE - 10U * 86400U;
  assert(history.confirm_time(now, 2026, 290, 10, true));
  assert(history.hourly_count() == 0 && history.daily_count() == 0);
  assert(history.record(now, 2026, 290, 10, 21, 51, true));
  assert(history.flush());
  assert(last_day(history)->valid_count == 1);
  unchanged_archive(archived);
}

/** An unreadable selection never silently falls back to writing bank0. */
static void test_corrupt_marker_blocks() {
  reset_n7();
  marker_host::namespace_exists = marker_host::blob_exists = true;
  marker_host::blob.assign(7, 0);
  RoomAggregateHistory history;
  history.setup();
  assert(history.recovery_blocked() && host_live_bytes == 0);
  assert(!history.record(BASE, 2026, 300, 10, 20, 50, true));
  assert(!history.confirm_time(BASE, 2026, 300, 10, true));
  history.on_shutdown();
  assert(marker_host::writes == 0 && esphome::host_preferences.save_calls == 0);
}

/** A failed cold-boot allocation can retry restoring the same committed archive. */
static void test_restore_allocation_retry() {
  reset_n7();
  {
    RoomAggregateHistory history;
    history.setup();
    seed_mixed(history);
    assert(history.flush());
    assert(history.confirm_time(BASE + 7200, 2026, 300, 12, true));
  }
  host_alloc_fail_after = 0;
  RoomAggregateHistory restored;
  restored.setup();
  assert(restored.recovery_blocked());
  const auto made = esphome::host_preferences.make_calls;
  for (unsigned retry = 0; retry < 20; ++retry)
    assert(!restored.confirm_time(BASE + 7200, 2026, 300, 12, true));
  assert(esphome::host_preferences.make_calls == made);
  host_alloc_fail_after = -1;
  assert(restored.confirm_time(BASE + 7200, 2026, 300, 12, true));
  assert(!restored.recovery_blocked());
  correct_prefix(restored);
}

/** Keep two immutable archives; refuse only a third rollback. */
static void test_two_archives_capacity_preserves_data() {
  reset_n7();
  SavedPages first_archive;
  SavedPages second_archive;
  std::vector<uint8_t> marker_v2;
  {
    RoomAggregateHistory history;
    history.setup();
    seed_mixed(history);
    assert(history.flush());
    first_archive = esphome::host_preferences.persisted;
    assert(history.confirm_time(BASE + 7200, 2026, 300, 12, true));
    assert(history.record(BASE + 365U * 86400U, 2027, 300, 10, 99, 99, true));
    assert(history.flush());
    second_archive = esphome::host_preferences.persisted;
    assert(history.confirm_time(BASE + 7200, 2026, 300, 12, true));
    Marker marker{};
    assert(load(marker) == State::READY && marker.version == VERSION_V2);
    assert(marker.active_bank == 2U && marker_host::writes == 2U);
    assert(marker.cutoff_day == marker.second_cutoff_day);
    marker_v2 = marker_host::blob;
    unchanged_archive(second_archive);
    correct_prefix(history);
  }
  RoomAggregateHistory restored;
  restored.setup();
  correct_prefix(restored);
  assert(marker_host::blob == marker_v2);
  unchanged_archive(first_archive);
  unchanged_archive(second_archive);
  assert(restored.record(BASE + 2U * 365U * 86400U, 2028, 300, 10, 98, 98, true));
  assert(restored.flush());
  const auto before_third = esphome::host_preferences.persisted;
  assert(!restored.confirm_time(BASE + 7200, 2026, 300, 12, true));
  assert(restored.archive_capacity_reached() && !restored.storage_ok());
  restored.on_shutdown();
  assert(marker_host::blob == marker_v2 && marker_host::writes == 2U);
  assert(esphome::host_preferences.persisted == before_third);
  unchanged_archive(second_archive);
}

/** A later correction can be earlier; both cutoffs remain durable. */
static void test_second_cutoff_can_move_earlier() {
  reset_n7();
  RoomAggregateHistory history;
  history.setup();
  seed_mixed(history);
  assert(history.flush());
  assert(history.confirm_time(BASE + 7200, 2026, 300, 12, true));
  assert(history.record(BASE + 365U * 86400U, 2027, 300, 10, 99, 99, true));
  assert(history.flush());
  const uint32_t earlier = BASE - 86400U;
  assert(history.confirm_time(earlier, 2026, 299, 12, true));
  Marker marker{};
  assert(load(marker) == State::READY && marker.version == VERSION_V2);
  assert(marker.cutoff_day == RoomAggregateHistory::local_day_key(2026, 300));
  assert(marker.second_cutoff_day == RoomAggregateHistory::local_day_key(2026, 299));
  assert(history.hourly_count() == 0 && history.daily_count() == 0);
}

/** An uncertain v2 readback blocks writes until the durable marker is readable. */
static void test_second_marker_uncertain_readback() {
  reset_n7();
  RoomAggregateHistory history;
  history.setup();
  seed_mixed(history);
  assert(history.flush());
  assert(history.confirm_time(BASE + 7200, 2026, 300, 12, true));
  assert(history.record(BASE + 365U * 86400U, 2027, 300, 10, 99, 99, true));
  assert(history.flush());
  const auto archive = esphome::host_preferences.persisted;
  marker_host::read_error_after_write = true;
  assert(!history.confirm_time(BASE + 7200, 2026, 300, 12, true));
  assert(history.recovery_blocked() && marker_host::blob.size() == sizeof(MarkerV2));
  unchanged_archive(archive);
  marker_host::read_error = 0;
  marker_host::read_error_after_write = false;
  assert(history.confirm_time(BASE + 7200, 2026, 300, 12, true));
  assert(!history.recovery_blocked() && marker_host::writes == 2U);
  correct_prefix(history);
  unchanged_archive(archive);
}

/** Freeze only an unknown partial hour; daily accumulation resumes with it. */
static void test_partial_hour_fence() {
  reset_n7();
  {
    RoomAggregateHistory history;
    history.setup();
    assert(history.record(BASE, 2026, 300, 10, 20, 50, true));
    assert(history.record(BASE + 3600, 2026, 300, 11, 24, 54, true));
    assert(history.record(BASE + 7200, 2026, 300, 12, 26, 56, true));
    assert(history.record(BASE + 8 * 3600, 2026, 300, 18, 99, 99, true));
    assert(history.flush());
    assert(history.confirm_time(BASE + 7200, 2026, 300, 12, true));
    assert(last_day(history)->valid_count == 3);
    assert(!history.record(BASE + 7500, 2026, 300, 12, 27, 57, true));
    assert(history.record(BASE + 3 * 3600, 2026, 300, 13, 28, 58, true));
    assert(history.flush());
    assert(last_day(history)->valid_count == 4);
  }
  RoomAggregateHistory restored;
  restored.setup();
  assert(last_day(restored)->valid_count == 4);
  assert(last_day(restored)->temperature_sum_centi == 9800);
}

/** Healthy clock notifications must not clear an unrelated allocation fault. */
static void test_sync_preserves_allocation_warning() {
  reset_n7();
  RoomAggregateHistory history;
  history.setup();
  host_alloc_fail_after = 0;
  assert(!history.record(BASE, 2026, 300, 10, 20, 50, true));
  assert(!history.storage_ok());
  assert(history.confirm_time(BASE, 2026, 300, 10, true));
  assert(!history.storage_ok());
  host_alloc_fail_after = -1;
  assert(history.record(BASE, 2026, 300, 10, 20, 50, true));
  assert(history.flush() && history.storage_ok());
}

int main(int argc, char **argv) {
  assert(argc == 3);
  test_dormant_no_authority();
  test_sync_preserves_allocation_warning();
  test_prefix_and_marker_only_reboot();
  test_current_series_reboot();
  test_partial_hour_fence();
  test_partial_sync_retry();
  test_uncertain_marker_readback();
  test_marker_write_retry();
  test_distant_future_window();
  test_legacy_future(argv[1]);
  test_corrupt_marker_blocks();
  test_restore_allocation_retry();
  test_two_archives_capacity_preserves_data();
  test_second_cutoff_can_move_earlier();
  test_second_marker_uncertain_readback();
  assert(host_live_bytes == 0);
  std::cout << "ROOM_CLOCK_RECOVERY_V2=PASS cases15 correct_prefix mixed_day marker_only"
            << " reboot dedup partial_sync uncertain_marker legacy future_window"
            << " corrupt_marker allocation_retry two_archives earlier_cutoff v2_uncertain"
            << " bounded_third no_device\n";
}
