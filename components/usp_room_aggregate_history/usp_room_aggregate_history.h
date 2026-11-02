#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "clock-classifier.h"
#include "marker-store.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::usp_room_aggregate_history {

static constexpr uint32_t RAW_BUCKET_SECONDS = 300;
static constexpr uint32_t CHECKPOINT_MS = 6U * 60U * 60U * 1000U;
static constexpr size_t HOUR_RING_CAPACITY = 32U * 24U;
static constexpr size_t DAY_RING_CAPACITY = 12U * 31U;
static constexpr size_t HOUR_PAGE_SLOTS = 32;
static constexpr size_t DAY_PAGE_SLOTS = 13;
static constexpr size_t LEGACY_HOUR_PAGE_BYTES = 592;
static constexpr size_t LEGACY_DAY_PAGE_BYTES = 760;
static constexpr size_t HOUR_PAGE_BYTES = 596;
static constexpr size_t DAY_PAGE_BYTES = 764;
static constexpr size_t NVS_PAYLOAD_BYTES =
    HOUR_PAGE_SLOTS * HOUR_PAGE_BYTES + DAY_PAGE_SLOTS * DAY_PAGE_BYTES;

struct Aggregate {
  uint32_t bucket{0};
  int32_t temperature_sum_centi{0};
  uint32_t humidity_sum_centi{0};
  int16_t temperature_min_centi{0};
  int16_t temperature_max_centi{0};
  uint16_t humidity_min_centi{0};
  uint16_t humidity_max_centi{0};
  uint16_t valid_count{0};
  uint16_t gap_count{0};
};

static_assert(sizeof(Aggregate) == 24, "Aggregate layout changed");

class RoomAggregateHistory final : public Component {
 public:
  ~RoomAggregateHistory();
  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  bool record(uint32_t epoch, uint16_t year, uint16_t day_of_year,
              uint8_t hour, float temperature_c, float humidity_pct,
              bool valid);
  /** Accept a clock correction only from an explicit synchronization. */
  bool confirm_time(uint32_t epoch, uint16_t year, uint16_t day_of_year,
                    uint8_t hour, bool synchronized);
  bool recovery_active() const { return this->recovery_active_; }
  bool recovery_blocked() const { return this->recovery_blocked_; }
  bool archive_capacity_reached() const { return this->archive_full_; }

  bool flush();

  const Aggregate *hourly_sample(size_t oldest_index) const;
  const Aggregate *daily_sample(size_t oldest_index) const;
  size_t hourly_count() const { return this->hourly_count_; }
  size_t daily_count() const { return this->daily_count_; }
  size_t active_psram_bytes() const;
  uint32_t save_count() const { return this->save_count_; }
  uint32_t corrupt_pages() const { return this->corrupt_pages_; }
  bool active() const { return this->active_; }
  bool loaded_from_nvs() const { return this->loaded_from_nvs_; }
  bool storage_ok() const { return this->storage_ok_; }

  static uint32_t local_day_key(uint16_t year, uint16_t day_of_year);

 protected:
  struct PendingSample {
    uint32_t raw_bucket{0};
    uint32_t epoch{0};
    uint32_t day_key{0};
    uint8_t hour{0};
    float temperature_c{0.0f};
    float humidity_pct{0.0f};
    bool valid{false};
    bool set{false};
  };

  bool load_recovery_state_();
  bool block_recovery_();
  void reset_series_();
  usp_room_clock_recovery::StoredExtent stored_extent_() const;
  bool settle_recovery_(bool allow_empty);
  bool activate_recovery_(const usp_room_recovery_store::Marker &marker);
  bool ensure_restore_storage_();
  bool restore_archive_();
  bool restore_archive_bank_(uint32_t bank, uint32_t cutoff_hour,
                             uint32_t cutoff_day);
  bool restore_archived_hour_page_(uint32_t page_id, uint32_t bank,
                                   uint32_t cutoff_hour);
  bool restore_archived_day_page_(uint32_t page_id, uint32_t bank,
                                  uint32_t cutoff_day);
  bool rebuild_archived_day_(uint32_t cutoff_day);
  static bool merge_aggregate_(Aggregate *target, const Aggregate &source);
  void finish_archive_();

  bool allocate_storage_();
  void release_storage_();
  bool restore_();
  void restore_hour_pages_(uint32_t newest);
  void restore_day_pages_(uint32_t newest);
  bool finalize_pending_();
  bool set_pending_(uint32_t epoch, uint32_t day_key, uint8_t hour,
                    float temperature_c, float humidity_pct, bool valid);
  bool can_record_calendar_(uint32_t day, uint8_t hour) const;
  bool hour_migration_ready_(uint32_t bucket) const;
  bool day_migration_ready_(uint32_t bucket, uint8_t hour) const;
  bool flush_due_() const;
  bool retry_due_(uint32_t now) const;
  void note_save_failure_(uint32_t now);
  void reset_save_retry_();
  void mark_hour_dirty_(uint32_t page_id);
  void mark_day_dirty_(uint32_t page_id);
  bool save_dirty_pages_();
  bool save_hour_page_(uint32_t page_id);
  bool save_day_page_(uint32_t page_id);
  void init_preferences_();

  Aggregate *upsert_hour_(uint32_t bucket);
  Aggregate *upsert_day_(uint32_t bucket);
  const Aggregate *hour_at_(size_t oldest_index) const;
  const Aggregate *day_at_(size_t oldest_index) const;
  static bool sample_usable_(float temperature_c, float humidity_pct,
                             bool valid);
  static void add_sample_(Aggregate *aggregate, float temperature_c,
                          float humidity_pct, bool valid);

  Aggregate *hourly_{nullptr};
  Aggregate *daily_{nullptr};
  size_t hourly_head_{0};
  size_t hourly_count_{0};
  size_t daily_head_{0};
  size_t daily_count_{0};
  PendingSample pending_{};

  std::array<ESPPreferenceObject, HOUR_PAGE_SLOTS> hour_preferences_{};
  std::array<ESPPreferenceObject, DAY_PAGE_SLOTS> day_preferences_{};
  uint32_t current_hour_page_id_{0};
  uint32_t previous_hour_page_id_{0};
  uint32_t current_day_page_id_{0};
  uint32_t previous_day_page_id_{0};
  bool hour_page_known_{false};
  bool day_page_known_{false};
  bool current_hour_dirty_{false};
  bool previous_hour_dirty_{false};
  bool current_day_dirty_{false};
  bool previous_day_dirty_{false};
  std::array<uint32_t, HOUR_PAGE_SLOTS> hour_page_cursors_{};
  std::array<uint32_t, DAY_PAGE_SLOTS> day_page_cursors_{};
  uint32_t finalized_cursor_{0};
  uint32_t legacy_hour_fence_{0};
  uint32_t legacy_day_fence_{0};
  bool have_legacy_hour_fence_{false};
  bool have_legacy_day_fence_{false};

  bool preferences_initialized_{false};
  uint8_t preferences_bank_{255};
  usp_room_recovery_store::Marker recovery_marker_{};
  bool recovery_active_{false};
  bool recovery_blocked_{false};
  bool recovery_restore_pending_{false};
  bool archive_full_{false};

  uint32_t last_checkpoint_ms_{0};
  uint32_t last_save_attempt_ms_{0};
  uint32_t retry_delay_ms_{30000};
  uint32_t save_count_{0};
  uint32_t corrupt_pages_{0};
  bool active_{false};
  bool loaded_from_nvs_{false};
  bool storage_ok_{true};
  bool save_failed_{false};
  bool allocation_failed_{false};
};

}  // namespace esphome::usp_room_aggregate_history
