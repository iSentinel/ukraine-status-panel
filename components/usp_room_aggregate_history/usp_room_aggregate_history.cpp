#include "usp_room_aggregate_history.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esp_heap_caps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace esphome::usp_room_aggregate_history {

static const char *const TAG = "usp.room_aggregate";
static constexpr uint32_t HOUR_MAGIC = 0x48524741;
static constexpr uint32_t DAY_MAGIC = 0x44524741;
static constexpr uint16_t PAGE_VERSION = 2;
static constexpr uint32_t MIN_RETRY_MS = 30000;
static constexpr uint32_t MAX_RETRY_MS = 300000;

namespace {

template<size_t Count, uint32_t Magic> struct LegacyPage {
  uint32_t magic{Magic};
  uint16_t version{1};
  uint16_t count{0};
  uint32_t page_id{0};
  Aggregate points[Count]{};
  uint32_t checksum{0};
};

template<size_t Count, uint32_t Magic> struct CursorPage {
  uint32_t magic{Magic};
  uint16_t version{PAGE_VERSION};
  uint16_t count{0};
  uint32_t page_id{0};
  Aggregate points[Count]{};
  uint32_t last_raw_bucket_plus_one{0};
  uint32_t checksum{0};
};

using HourPage = CursorPage<24, HOUR_MAGIC>;
using DayPage = CursorPage<31, DAY_MAGIC>;
using LegacyHourPage = LegacyPage<24, HOUR_MAGIC>;
using LegacyDayPage = LegacyPage<31, DAY_MAGIC>;

static_assert(sizeof(HourPage) == HOUR_PAGE_BYTES, "HourPage layout changed");
static_assert(sizeof(DayPage) == DAY_PAGE_BYTES, "DayPage layout changed");
static_assert(sizeof(LegacyHourPage) == LEGACY_HOUR_PAGE_BYTES, "Legacy hour layout changed");
static_assert(sizeof(LegacyDayPage) == LEGACY_DAY_PAGE_BYTES, "Legacy day layout changed");

uint32_t checksum_bytes(const void *data, size_t size) {
  const auto *bytes = static_cast<const uint8_t *>(data);
  uint32_t hash = 2166136261U;
  for (size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 16777619U;
  }
  return hash;
}

/** Match both formats' checksum without a second page-sized stack copy. */
template<typename Page> uint32_t page_checksum(const Page &page) {
  constexpr size_t offset = offsetof(Page, checksum);
  static_assert(offset + sizeof(page.checksum) == sizeof(Page));
  uint32_t hash = checksum_bytes(&page, offset);
  for (size_t index = 0; index < sizeof(page.checksum); ++index)
    hash *= 16777619U;
  return hash;
}

bool aggregate_nonempty(const Aggregate &point) {
  return point.valid_count != 0 || point.gap_count != 0;
}

bool aggregate_valid(const Aggregate &point, uint16_t maximum_samples) {
  const uint32_t total = static_cast<uint32_t>(point.valid_count) + point.gap_count;
  if (total == 0 || total > maximum_samples) return false;
  if (point.valid_count == 0) return true;
  return point.temperature_min_centi <= point.temperature_max_centi &&
         point.humidity_min_centi <= point.humidity_max_centi &&
         point.humidity_max_centi <= 10000U;
}


template<typename Page>
bool checksum_valid(const Page &page, uint16_t version) {
  if (version == PAGE_VERSION) return page.checksum == page_checksum(page);
  if (version != 1) return false;
  constexpr size_t offset = offsetof(Page, last_raw_bucket_plus_one);
  uint32_t hash = checksum_bytes(&page, offset);
  for (size_t index = 0; index < sizeof(uint32_t); ++index)
    hash *= 16777619U;
  return page.last_raw_bucket_plus_one == hash;
}

template<typename Page>
bool valid_points(const Page &page, uint32_t magic, uint16_t version,
                  uint16_t maximum_samples) {
  constexpr size_t slots = sizeof(page.points) / sizeof(page.points[0]);
  if (page.magic != magic || page.version != version || page.count > slots ||
      !checksum_valid(page, version)) return false;
  uint16_t seen = 0;
  for (size_t index = 0; index < slots; ++index) {
    const auto &point = page.points[index];
    if (!aggregate_nonempty(point)) continue;
    if (!aggregate_valid(point, maximum_samples) ||
        point.bucket / slots != page.page_id || point.bucket % slots != index)
      return false;
    ++seen;
  }
  return seen == page.count;
}

template<typename Page> bool valid_cursor(const Page &page) {
  if (page.count == 0) return page.last_raw_bucket_plus_one == 0;
  return page.last_raw_bucket_plus_one != 0 &&
         page.last_raw_bucket_plus_one <=
             std::numeric_limits<uint32_t>::max() / RAW_BUCKET_SECONDS + 1U;
}

bool valid_hour_page(const HourPage &page) {
  return valid_points(page, HOUR_MAGIC, PAGE_VERSION, 24) && valid_cursor(page);
}

bool valid_day_page(const DayPage &page) {
  return valid_points(page, DAY_MAGIC, PAGE_VERSION, 300) && valid_cursor(page);
}

bool valid_legacy_hour(const HourPage &page) {
  return valid_points(page, HOUR_MAGIC, 1, 24);
}

bool valid_legacy_day(const DayPage &page) {
  return valid_points(page, DAY_MAGIC, 1, 300);
}

/** Read old bytes by their own size; never erase a page during migration. */
template<typename Page, typename Legacy, typename Preference>
bool load_compatible(Preference preference, Page &page,
                     bool (*current_valid)(const Page &),
                     bool (*legacy_valid)(const Page &), bool &corrupt) {
  const bool current_loaded = preference.load(&page);
  if (current_loaded && current_valid(page)) return true;
  static_assert(offsetof(Page, last_raw_bucket_plus_one) ==
                offsetof(Legacy, checksum));
  static_assert(sizeof(Legacy) + sizeof(uint32_t) == sizeof(Page));
  // The old layout is an exact byte prefix of the already allocated page.
  const bool legacy_loaded = preference.load(
      reinterpret_cast<uint8_t *>(&page), sizeof(Legacy));
  if (legacy_loaded && legacy_valid(page)) {
    page.version = PAGE_VERSION;
    page.last_raw_bucket_plus_one = 0;
    page.checksum = 0;
    return true;
  }
  corrupt = current_loaded || legacy_loaded;
  return false;
}

template<typename Page, typename Legacy, size_t Slots>
bool newest_page(std::array<ESPPreferenceObject, Slots> &preferences,
                 bool (*current_valid)(const Page &),
                 bool (*legacy_valid)(const Page &),
                 uint32_t &newest, uint32_t &corrupt_pages) {
  bool found = false;
  for (size_t slot = 0; slot < Slots; ++slot) {
    Page page{};
    bool corrupt = false;
    if (!load_compatible<Page, Legacy>(preferences[slot], page,
                                       current_valid, legacy_valid, corrupt)) {
      if (corrupt) ++corrupt_pages;
      continue;
    }
    if (page.page_id % Slots != slot) {
      ++corrupt_pages;
      continue;
    }
    if (page.count == 0) continue;
    if (!found || page.page_id > newest) newest = page.page_id;
    found = true;
  }
  return found;
}


/** One-shot archive read uses the platform API without allocating a backend. */
struct ArchivePreference {
  uint32_t key;
  template<typename T> bool load(T *value) const {
    return this->load(reinterpret_cast<uint8_t *>(value), sizeof(T));
  }
  bool load(uint8_t *value, size_t size) const {
    return global_preferences->load_from_key(this->key, value, size);
  }
};

void series_key(char *key, size_t size, bool hour, size_t slot, uint32_t bank) {
  if (bank == 0U) {
    std::snprintf(key, size, hour ? "usp_room_agg_h%02u" : "usp_room_agg_d%02u",
                  static_cast<unsigned>(slot));
    return;
  }
  std::snprintf(key, size, hour ? "usp_room_agg%u_h%02u" : "usp_room_agg%u_d%02u",
                static_cast<unsigned>(bank), static_cast<unsigned>(slot));
}

ArchivePreference archived_preference(bool hour, size_t slot, uint32_t bank) {
  char key[32];
  series_key(key, sizeof(key), hour, slot, bank);
  return {fnv1_hash(key)};
}

ESPPreferenceObject recovery_preference(bool hour, size_t slot, uint32_t bank) {
  char key[32];
  series_key(key, sizeof(key), hour, slot, bank);
  return hour ? global_preferences->make_preference<HourPage>(fnv1_hash(key), true)
              : global_preferences->make_preference<DayPage>(fnv1_hash(key), true);
}

bool is_leap_year(uint16_t year) {
  return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

uint32_t leap_years_before(uint32_t year) {
  if (year == 0) return 0;
  const uint32_t previous = year - 1U;
  return previous / 4U - previous / 100U + previous / 400U;
}

}  // namespace

RoomAggregateHistory::~RoomAggregateHistory() { this->release_storage_(); }

void RoomAggregateHistory::setup() {
  if (!this->load_recovery_state_()) return;
  this->init_preferences_();
  if (!this->restore_()) {
    this->recovery_restore_pending_ = this->recovery_active_;
    this->block_recovery_();
  }
  if (!this->active_) {
    ESP_LOGI(TAG, "Aggregate history dormant; waiting for first valid room sample");
  }
}

void RoomAggregateHistory::loop() {
  if (this->recovery_blocked_) return;
  if (!this->active_) return;
  const uint32_t now = millis();
  if (this->save_failed_) {
    if (this->retry_due_(now)) this->save_dirty_pages_();
    return;
  }
  const bool page_rotation = this->previous_hour_dirty_ || this->previous_day_dirty_;
  if (!page_rotation && !this->flush_due_()) return;
  this->save_dirty_pages_();
}

void RoomAggregateHistory::on_shutdown() {
  if (this->recovery_blocked_) return;
  if (!this->active_) return;
  if (!this->finalize_pending_()) {
    ESP_LOGW(TAG, "Aggregate shutdown pending sample rejected");
  }
  if (!this->save_dirty_pages_()) {
    ESP_LOGW(TAG, "Aggregate shutdown persistence failed");
  }
}

void RoomAggregateHistory::dump_config() {
  ESP_LOGCONFIG(TAG, "Room aggregate history:");
  ESP_LOGCONFIG(TAG, "  Active: %s", YESNO(this->active_));
  ESP_LOGCONFIG(TAG, "  Loaded from NVS: %s", YESNO(this->loaded_from_nvs_));
  ESP_LOGCONFIG(TAG, "  Hourly RAM slots: %u", static_cast<unsigned>(HOUR_RING_CAPACITY));
  ESP_LOGCONFIG(TAG, "  Daily RAM slots: %u", static_cast<unsigned>(DAY_RING_CAPACITY));
  ESP_LOGCONFIG(TAG, "  Active PSRAM bytes: %u", static_cast<unsigned>(this->active_psram_bytes()));
  ESP_LOGCONFIG(TAG, "  NVS page payload budget: %u bytes", static_cast<unsigned>(NVS_PAYLOAD_BYTES));
  ESP_LOGCONFIG(TAG, "  Checkpoint: 6h plus page rotation/shutdown");
  ESP_LOGCONFIG(TAG, "  Corrupt pages ignored: %lu", static_cast<unsigned long>(this->corrupt_pages_));
  ESP_LOGCONFIG(TAG, "  Successful persistence checkpoints: %lu", static_cast<unsigned long>(this->save_count_));
}

bool RoomAggregateHistory::record(uint32_t epoch, uint16_t year,
                                  uint16_t day_of_year, uint8_t hour,
                                  float temperature_c, float humidity_pct,
                                  bool valid) {
  if (this->recovery_blocked_) return false;
  if (epoch == 0 || year < 1970 || hour >= 24) return false;
  const uint16_t days = is_leap_year(year) ? 366 : 365;
  if (day_of_year == 0 || day_of_year > days) return false;
  const bool usable = sample_usable_(temperature_c, humidity_pct, valid);
  if (!this->active_) {
    if (!usable || !this->allocate_storage_()) return false;
    this->active_ = true;
    this->last_checkpoint_ms_ = millis();
  }
  const uint32_t raw_bucket = epoch / RAW_BUCKET_SECONDS;
  const uint32_t day_key = local_day_key(year, day_of_year);
  if (raw_bucket + 1U <= this->finalized_cursor_) return false;
  if (!this->can_record_calendar_(day_key, hour)) return false;
  return this->set_pending_(epoch, day_key, hour,
                            temperature_c, humidity_pct, valid);
}

/** The latest observation wins until its five-minute interval is finalized. */
bool RoomAggregateHistory::set_pending_(uint32_t epoch, uint32_t day_key,
                                       uint8_t hour, float temperature_c,
                                       float humidity_pct, bool valid) {
  const uint32_t raw_bucket = epoch / RAW_BUCKET_SECONDS;
  if (this->pending_.set) {
    if (raw_bucket < this->pending_.raw_bucket ||
        (raw_bucket == this->pending_.raw_bucket && epoch < this->pending_.epoch))
      return false;
    if (raw_bucket != this->pending_.raw_bucket && !this->finalize_pending_())
      return false;
  }
  this->pending_ = {raw_bucket, epoch, day_key, hour,
                    temperature_c, humidity_pct, valid, true};
  return true;
}

bool RoomAggregateHistory::hour_migration_ready_(uint32_t bucket) const {
  return !this->have_legacy_hour_fence_ || bucket > this->legacy_hour_fence_;
}

bool RoomAggregateHistory::day_migration_ready_(uint32_t bucket, uint8_t hour) const {
  if (!this->have_legacy_day_fence_ || bucket > this->legacy_day_fence_) return true;
  return this->recovery_active_ &&
         bucket == usp_room_recovery_store::selected_cutoff_day(this->recovery_marker_) &&
         bucket == this->legacy_day_fence_ &&
         this->hour_migration_ready_(bucket * 24U + hour);
}

/** Reject old calendar input before it can leave an unflushable pending sample. */
bool RoomAggregateHistory::can_record_calendar_(uint32_t day, uint8_t hour) const {
  const uint32_t hour_bucket = day * 24U + hour;
  const bool add_hour = this->hour_migration_ready_(hour_bucket);
  const bool add_day = this->day_migration_ready_(day, hour);
  const auto *last_hour = this->hour_at_(this->hourly_count_ - 1U);
  const auto *last_day = this->day_at_(this->daily_count_ - 1U);
  if (add_hour && last_hour != nullptr && hour_bucket < last_hour->bucket)
    return false;
  if (add_day && last_day != nullptr && day < last_day->bucket) return false;
  return add_hour || add_day;
}


bool RoomAggregateHistory::flush() {
  if (this->recovery_blocked_) return false;
  if (!this->active_) return true;
  if (!this->finalize_pending_()) return false;
  return this->save_dirty_pages_();
}

const Aggregate *RoomAggregateHistory::hourly_sample(size_t oldest_index) const {
  return this->hour_at_(oldest_index);
}

const Aggregate *RoomAggregateHistory::daily_sample(size_t oldest_index) const {
  return this->day_at_(oldest_index);
}

size_t RoomAggregateHistory::active_psram_bytes() const {
  if (!this->active_ || this->hourly_ == nullptr || this->daily_ == nullptr) return 0;
  return (HOUR_RING_CAPACITY + DAY_RING_CAPACITY) * sizeof(Aggregate);
}

uint32_t RoomAggregateHistory::local_day_key(uint16_t year, uint16_t day_of_year) {
  if (year < 1970 || day_of_year == 0) return 0;
  const uint16_t days = is_leap_year(year) ? 366 : 365;
  if (day_of_year > days) return 0;
  return (static_cast<uint32_t>(year) - 1970U) * 365U +
         leap_years_before(year) - leap_years_before(1970U) +
         static_cast<uint32_t>(day_of_year) - 1U;
}

bool RoomAggregateHistory::allocate_storage_() {
  if (this->hourly_ != nullptr && this->daily_ != nullptr) return true;
  this->release_storage_();
  this->hourly_ = static_cast<Aggregate *>(
      heap_caps_calloc(HOUR_RING_CAPACITY, sizeof(Aggregate),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->hourly_ == nullptr) {
    this->allocation_failed_ = true;
    this->storage_ok_ = false;
    this->status_set_warning("Aggregate history PSRAM allocation failed");
    return false;
  }
  this->daily_ = static_cast<Aggregate *>(
      heap_caps_calloc(DAY_RING_CAPACITY, sizeof(Aggregate),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->daily_ == nullptr) {
    this->release_storage_();
    this->allocation_failed_ = true;
    this->storage_ok_ = false;
    this->status_set_warning("Aggregate history PSRAM allocation failed");
    return false;
  }
  this->allocation_failed_ = false;
  this->storage_ok_ = true;
  this->status_clear_warning();
  return true;
}

void RoomAggregateHistory::release_storage_() {
  if (this->hourly_ != nullptr) heap_caps_free(this->hourly_);
  if (this->daily_ != nullptr) heap_caps_free(this->daily_);
  this->hourly_ = nullptr;
  this->daily_ = nullptr;
  this->hourly_head_ = 0;
  this->hourly_count_ = 0;
  this->daily_head_ = 0;
  this->daily_count_ = 0;
  this->reset_series_();
}

void RoomAggregateHistory::init_preferences_() {
  const uint32_t bank = this->recovery_active_
      ? usp_room_recovery_store::active_bank(this->recovery_marker_) : 0U;
  if (this->preferences_initialized_ && this->preferences_bank_ == bank) return;
  for (size_t i = 0; i < HOUR_PAGE_SLOTS; ++i)
    this->hour_preferences_[i] = recovery_preference(true, i, bank);
  for (size_t i = 0; i < DAY_PAGE_SLOTS; ++i)
    this->day_preferences_[i] = recovery_preference(false, i, bank);
  this->preferences_initialized_ = true;
  this->preferences_bank_ = static_cast<uint8_t>(bank);
}

bool RoomAggregateHistory::restore_() {
  if (this->recovery_active_ && !this->restore_archive_()) return false;
  uint32_t newest_hour = 0;
  uint32_t newest_day = 0;
  const bool have_hour = newest_page<HourPage, LegacyHourPage>(
      this->hour_preferences_, valid_hour_page, valid_legacy_hour,
      newest_hour, this->corrupt_pages_);
  const bool have_day = newest_page<DayPage, LegacyDayPage>(
      this->day_preferences_, valid_day_page, valid_legacy_day,
      newest_day, this->corrupt_pages_);
  if (!have_hour && !have_day) return true;
  if (!this->allocate_storage_()) return false;
  this->active_ = true;
  if (have_hour) this->restore_hour_pages_(newest_hour);
  if (have_day) this->restore_day_pages_(newest_day);
  this->loaded_from_nvs_ = this->hourly_count_ != 0 || this->daily_count_ != 0;
  this->last_checkpoint_ms_ = millis();
  ESP_LOGI(TAG, "Aggregate restored hourly=%u daily=%u corrupt=%lu",
           static_cast<unsigned>(this->hourly_count_),
           static_cast<unsigned>(this->daily_count_),
           static_cast<unsigned long>(this->corrupt_pages_));
  return true;
}

/** Retain legacy hour totals; an unknown partial hour is never counted twice. */
void RoomAggregateHistory::restore_hour_pages_(uint32_t newest) {
  const uint32_t first = newest >= HOUR_PAGE_SLOTS - 1
      ? newest - static_cast<uint32_t>(HOUR_PAGE_SLOTS - 1) : 0;
  for (uint32_t page_id = first; page_id <= newest; ++page_id) {
    HourPage page{};
    bool corrupt = false;
    const size_t slot = page_id % HOUR_PAGE_SLOTS;
    if (!load_compatible<HourPage, LegacyHourPage>(
            this->hour_preferences_[slot], page, valid_hour_page,
            valid_legacy_hour, corrupt) || page.page_id != page_id) continue;
    this->hour_page_cursors_[slot] = page.last_raw_bucket_plus_one;
    this->finalized_cursor_ = std::max(this->finalized_cursor_,
                                     page.last_raw_bucket_plus_one);
    for (const auto &point : page.points) {
      if (!aggregate_nonempty(point)) continue;
      if (this->recovery_active_ && this->have_legacy_hour_fence_ &&
          point.bucket <= this->legacy_hour_fence_) continue;
      Aggregate *target = this->upsert_hour_(point.bucket);
      if (target != nullptr) *target = point;
      if (page.last_raw_bucket_plus_one == 0) {
        this->legacy_hour_fence_ = std::max(this->legacy_hour_fence_, point.bucket);
        this->have_legacy_hour_fence_ = true;
      }
    }
  }
  this->current_hour_page_id_ = newest;
  this->hour_page_known_ = true;
}

/** Retain legacy day totals until a new local day makes new samples unambiguous. */
void RoomAggregateHistory::restore_day_pages_(uint32_t newest) {
  const uint32_t first = newest >= DAY_PAGE_SLOTS - 1
      ? newest - static_cast<uint32_t>(DAY_PAGE_SLOTS - 1) : 0;
  for (uint32_t page_id = first; page_id <= newest; ++page_id) {
    DayPage page{};
    bool corrupt = false;
    const size_t slot = page_id % DAY_PAGE_SLOTS;
    if (!load_compatible<DayPage, LegacyDayPage>(
            this->day_preferences_[slot], page, valid_day_page,
            valid_legacy_day, corrupt) || page.page_id != page_id) continue;
    this->day_page_cursors_[slot] = page.last_raw_bucket_plus_one;
    this->finalized_cursor_ = std::max(this->finalized_cursor_,
                                     page.last_raw_bucket_plus_one);
    for (const auto &point : page.points) {
      if (!aggregate_nonempty(point)) continue;
      if (this->recovery_active_ && this->have_legacy_day_fence_ &&
          point.bucket < this->legacy_day_fence_) continue;
      Aggregate *target = this->upsert_day_(point.bucket);
      if (target != nullptr) *target = point;
      if (page.last_raw_bucket_plus_one == 0) {
        this->legacy_day_fence_ = std::max(this->legacy_day_fence_, point.bucket);
        this->have_legacy_day_fence_ = true;
      }
    }
  }
  this->current_day_page_id_ = newest;
  this->day_page_known_ = true;
}


bool RoomAggregateHistory::finalize_pending_() {
  if (!this->pending_.set) return true;
  const uint32_t hour_bucket = this->pending_.day_key * 24U + this->pending_.hour;
  const bool add_hour = this->hour_migration_ready_(hour_bucket);
  const bool add_day = this->day_migration_ready_(this->pending_.day_key, this->pending_.hour);
  Aggregate *hour = add_hour ? this->upsert_hour_(hour_bucket) : nullptr;
  Aggregate *day = add_day ? this->upsert_day_(this->pending_.day_key) : nullptr;
  if ((add_hour && hour == nullptr) || (add_day && day == nullptr)) return false;
  const uint32_t cursor = this->pending_.raw_bucket + 1U;
  if (add_hour) {
    add_sample_(hour, this->pending_.temperature_c, this->pending_.humidity_pct,
                this->pending_.valid);
    const uint32_t page = hour_bucket / 24U;
    this->hour_page_cursors_[page % HOUR_PAGE_SLOTS] = cursor;
    this->mark_hour_dirty_(page);
  }
  if (add_day) {
    add_sample_(day, this->pending_.temperature_c, this->pending_.humidity_pct,
                this->pending_.valid);
    const uint32_t page = this->pending_.day_key / 31U;
    this->day_page_cursors_[page % DAY_PAGE_SLOTS] = cursor;
    this->mark_day_dirty_(page);
  }
  this->finalized_cursor_ = cursor;
  this->pending_.set = false;
  return true;
}


bool RoomAggregateHistory::flush_due_() const {
  return (this->current_hour_dirty_ || this->current_day_dirty_) &&
         millis() - this->last_checkpoint_ms_ >= CHECKPOINT_MS;
}

bool RoomAggregateHistory::retry_due_(uint32_t now) const {
  return now - this->last_save_attempt_ms_ >= this->retry_delay_ms_;
}

void RoomAggregateHistory::note_save_failure_(uint32_t now) {
  if (this->save_failed_) {
    const uint32_t doubled = this->retry_delay_ms_ * 2U;
    this->retry_delay_ms_ = doubled > MAX_RETRY_MS ? MAX_RETRY_MS : doubled;
  } else {
    this->retry_delay_ms_ = MIN_RETRY_MS;
  }
  this->last_save_attempt_ms_ = now;
  this->save_failed_ = true;
  this->storage_ok_ = false;
  this->status_set_warning("Aggregate history persistence failed");
}

void RoomAggregateHistory::reset_save_retry_() {
  this->last_save_attempt_ms_ = 0;
  this->retry_delay_ms_ = MIN_RETRY_MS;
  this->save_failed_ = false;
  this->storage_ok_ = !this->archive_full_;
  if (this->archive_full_)
    this->status_set_warning("Room history archive capacity reached");
  else
    this->status_clear_warning();
}

void RoomAggregateHistory::mark_hour_dirty_(uint32_t page_id) {
  if (!this->hour_page_known_) {
    this->current_hour_page_id_ = page_id;
    this->hour_page_known_ = true;
    this->current_hour_dirty_ = true;
    return;
  }
  if (page_id == this->current_hour_page_id_) {
    this->current_hour_dirty_ = true;
    return;
  }
  if (page_id < this->current_hour_page_id_) return;
  this->previous_hour_page_id_ = this->current_hour_page_id_;
  this->previous_hour_dirty_ = this->current_hour_dirty_;
  this->current_hour_page_id_ = page_id;
  this->current_hour_dirty_ = true;
}

void RoomAggregateHistory::mark_day_dirty_(uint32_t page_id) {
  if (!this->day_page_known_) {
    this->current_day_page_id_ = page_id;
    this->day_page_known_ = true;
    this->current_day_dirty_ = true;
    return;
  }
  if (page_id == this->current_day_page_id_) {
    this->current_day_dirty_ = true;
    return;
  }
  if (page_id < this->current_day_page_id_) return;
  this->previous_day_page_id_ = this->current_day_page_id_;
  this->previous_day_dirty_ = this->current_day_dirty_;
  this->current_day_page_id_ = page_id;
  this->current_day_dirty_ = true;
}

bool RoomAggregateHistory::save_dirty_pages_() {
  if (!this->active_) return true;
  bool queued = true;
  if (this->previous_hour_dirty_)
    queued = this->save_hour_page_(this->previous_hour_page_id_) && queued;
  if (this->current_hour_dirty_)
    queued = this->save_hour_page_(this->current_hour_page_id_) && queued;
  if (this->previous_day_dirty_)
    queued = this->save_day_page_(this->previous_day_page_id_) && queued;
  if (this->current_day_dirty_)
    queued = this->save_day_page_(this->current_day_page_id_) && queued;
  const bool synced = queued && global_preferences->sync();
  if (!synced) {
    this->note_save_failure_(millis());
    ESP_LOGW(TAG, "Aggregate NVS save failed; retry in %lu ms",
             static_cast<unsigned long>(this->retry_delay_ms_));
    return false;
  }
  this->previous_hour_dirty_ = false;
  this->current_hour_dirty_ = false;
  this->previous_day_dirty_ = false;
  this->current_day_dirty_ = false;
  this->last_checkpoint_ms_ = millis();
  this->reset_save_retry_();
  ++this->save_count_;
  ESP_LOGI(TAG, "Aggregate saved hourly=%u daily=%u checkpoints=%lu",
           static_cast<unsigned>(this->hourly_count_),
           static_cast<unsigned>(this->daily_count_),
           static_cast<unsigned long>(this->save_count_));
  return true;
}

bool RoomAggregateHistory::save_hour_page_(uint32_t page_id) {
  HourPage page{};
  page.page_id = page_id;
  page.last_raw_bucket_plus_one =
      this->hour_page_cursors_[page_id % HOUR_PAGE_SLOTS];
  for (size_t index = 0; index < this->hourly_count_; ++index) {
    const Aggregate *point = this->hour_at_(index);
    if (point == nullptr || point->bucket / 24U != page_id) continue;
    page.points[point->bucket % 24U] = *point;
    ++page.count;
  }
  page.checksum = page_checksum(page);
  return this->hour_preferences_[page_id % HOUR_PAGE_SLOTS].save(&page);
}

bool RoomAggregateHistory::save_day_page_(uint32_t page_id) {
  DayPage page{};
  page.page_id = page_id;
  page.last_raw_bucket_plus_one =
      this->day_page_cursors_[page_id % DAY_PAGE_SLOTS];
  for (size_t index = 0; index < this->daily_count_; ++index) {
    const Aggregate *point = this->day_at_(index);
    if (point == nullptr || point->bucket / 31U != page_id) continue;
    page.points[point->bucket % 31U] = *point;
    ++page.count;
  }
  page.checksum = page_checksum(page);
  return this->day_preferences_[page_id % DAY_PAGE_SLOTS].save(&page);
}

Aggregate *RoomAggregateHistory::upsert_hour_(uint32_t bucket) {
  if (this->hourly_ == nullptr) return nullptr;
  if (this->hourly_count_ != 0) {
    Aggregate &latest = this->hourly_[(this->hourly_head_ + HOUR_RING_CAPACITY - 1) %
                                     HOUR_RING_CAPACITY];
    if (bucket < latest.bucket) return nullptr;
    if (bucket == latest.bucket) return &latest;
  }
  Aggregate &next = this->hourly_[this->hourly_head_];
  next = Aggregate{};
  next.bucket = bucket;
  this->hourly_head_ = (this->hourly_head_ + 1) % HOUR_RING_CAPACITY;
  if (this->hourly_count_ < HOUR_RING_CAPACITY) ++this->hourly_count_;
  return &next;
}

Aggregate *RoomAggregateHistory::upsert_day_(uint32_t bucket) {
  if (this->daily_ == nullptr) return nullptr;
  if (this->daily_count_ != 0) {
    Aggregate &latest = this->daily_[(this->daily_head_ + DAY_RING_CAPACITY - 1) %
                                    DAY_RING_CAPACITY];
    if (bucket < latest.bucket) return nullptr;
    if (bucket == latest.bucket) return &latest;
  }
  Aggregate &next = this->daily_[this->daily_head_];
  next = Aggregate{};
  next.bucket = bucket;
  this->daily_head_ = (this->daily_head_ + 1) % DAY_RING_CAPACITY;
  if (this->daily_count_ < DAY_RING_CAPACITY) ++this->daily_count_;
  return &next;
}

const Aggregate *RoomAggregateHistory::hour_at_(size_t oldest_index) const {
  if (this->hourly_ == nullptr || oldest_index >= this->hourly_count_) return nullptr;
  const size_t oldest =
      (this->hourly_head_ + HOUR_RING_CAPACITY - this->hourly_count_) % HOUR_RING_CAPACITY;
  return &this->hourly_[(oldest + oldest_index) % HOUR_RING_CAPACITY];
}

const Aggregate *RoomAggregateHistory::day_at_(size_t oldest_index) const {
  if (this->daily_ == nullptr || oldest_index >= this->daily_count_) return nullptr;
  const size_t oldest =
      (this->daily_head_ + DAY_RING_CAPACITY - this->daily_count_) % DAY_RING_CAPACITY;
  return &this->daily_[(oldest + oldest_index) % DAY_RING_CAPACITY];
}

bool RoomAggregateHistory::sample_usable_(float temperature_c,
                                          float humidity_pct, bool valid) {
  if (!valid || !std::isfinite(temperature_c) || !std::isfinite(humidity_pct)) return false;
  constexpr float minimum = std::numeric_limits<int16_t>::lowest() / 100.0f;
  constexpr float maximum = std::numeric_limits<int16_t>::max() / 100.0f;
  return temperature_c >= minimum && temperature_c <= maximum &&
         humidity_pct >= 0.0f && humidity_pct <= 100.0f;
}

void RoomAggregateHistory::add_sample_(Aggregate *aggregate, float temperature_c,
                                       float humidity_pct, bool valid) {
  if (aggregate == nullptr) return;
  if (!sample_usable_(temperature_c, humidity_pct, valid)) {
    if (aggregate->gap_count != std::numeric_limits<uint16_t>::max())
      ++aggregate->gap_count;
    return;
  }
  const auto temperature = static_cast<int16_t>(
      std::lround(static_cast<double>(temperature_c) * 100.0));
  const auto humidity = static_cast<uint16_t>(
      std::lround(static_cast<double>(humidity_pct) * 100.0));
  if (aggregate->valid_count == 0) {
    aggregate->temperature_min_centi = temperature;
    aggregate->temperature_max_centi = temperature;
    aggregate->humidity_min_centi = humidity;
    aggregate->humidity_max_centi = humidity;
  } else {
    aggregate->temperature_min_centi =
        std::min(aggregate->temperature_min_centi, temperature);
    aggregate->temperature_max_centi =
        std::max(aggregate->temperature_max_centi, temperature);
    aggregate->humidity_min_centi =
        std::min(aggregate->humidity_min_centi, humidity);
    aggregate->humidity_max_centi =
        std::max(aggregate->humidity_max_centi, humidity);
  }
  aggregate->temperature_sum_centi += temperature;
  aggregate->humidity_sum_centi += humidity;
  if (aggregate->valid_count != std::numeric_limits<uint16_t>::max())
    ++aggregate->valid_count;
}

/** Load only metadata; absent optional storage does not create an NVS key. */
bool RoomAggregateHistory::load_recovery_state_() {
  using namespace usp_room_recovery_store;
  const State state = load(this->recovery_marker_);
  if (state == State::EMPTY) return true;
  if (state == State::READY) {
    this->recovery_active_ = true;
    return true;
  }
  return this->block_recovery_();
}

/** Stop writes when the selected series cannot be determined safely. */
bool RoomAggregateHistory::block_recovery_() {
  this->recovery_blocked_ = true;
  this->storage_ok_ = false;
  this->status_set_warning("Room history recovery unavailable");
  return false;
}

/** Clear only the working rings; archive selection and NVS bytes stay intact. */
void RoomAggregateHistory::reset_series_() {
  if (this->hourly_ != nullptr)
    std::fill_n(this->hourly_, HOUR_RING_CAPACITY, Aggregate{});
  if (this->daily_ != nullptr)
    std::fill_n(this->daily_, DAY_RING_CAPACITY, Aggregate{});
  this->hourly_head_ = this->hourly_count_ = 0;
  this->daily_head_ = this->daily_count_ = 0;
  this->pending_ = {};
  this->hour_page_cursors_.fill(0);
  this->day_page_cursors_.fill(0);
  this->finalized_cursor_ = 0;
  this->legacy_hour_fence_ = this->legacy_day_fence_ = 0;
  this->have_legacy_hour_fence_ = this->have_legacy_day_fence_ = false;
  this->hour_page_known_ = this->day_page_known_ = false;
  this->current_hour_dirty_ = this->previous_hour_dirty_ = false;
  this->current_day_dirty_ = this->previous_day_dirty_ = false;
  this->loaded_from_nvs_ = false;
  this->active_ = this->hourly_ != nullptr && this->daily_ != nullptr;
}

/** Include the unsaved pending sample when detecting an erroneous future. */
usp_room_clock_recovery::StoredExtent RoomAggregateHistory::stored_extent_() const {
  const auto *hour = this->hour_at_(this->hourly_count_ - 1U);
  const auto *day = this->day_at_(this->daily_count_ - 1U);
  usp_room_clock_recovery::StoredExtent result{
      this->finalized_cursor_, hour ? hour->bucket : 0,
      day ? day->bucket : 0, hour != nullptr, day != nullptr};
  if (this->pending_.set) {
    result.raw_cursor = std::max(result.raw_cursor, this->pending_.raw_bucket + 1U);
    result.newest_hour = std::max(result.newest_hour,
                                 this->pending_.day_key * 24U + this->pending_.hour);
    result.newest_day = std::max(result.newest_day, this->pending_.day_key);
    result.have_hour = result.have_day = true;
  }
  return result;
}

/** Read back a selection even when the preceding write reported an error. */
bool RoomAggregateHistory::settle_recovery_(bool allow_empty) {
  using namespace usp_room_recovery_store;
  Marker stored{};
  const State state = load(stored);
  if (state == State::EMPTY && allow_empty && !this->recovery_active_) {
    this->recovery_blocked_ = false;
    return true;
  }
  if (state != State::READY) return this->block_recovery_();
  if (!this->recovery_active_ || this->recovery_restore_pending_ ||
      usp_room_recovery_store::active_bank(stored) !=
          usp_room_recovery_store::active_bank(this->recovery_marker_))
    return this->activate_recovery_(stored);
  if (!usp_room_recovery_store::same(stored, this->recovery_marker_))
    return this->block_recovery_();
  this->recovery_blocked_ = false;
  return true;
}

/** Select bank1 only after the durable marker can be read and validated. */
bool RoomAggregateHistory::activate_recovery_(
    const usp_room_recovery_store::Marker &marker) {
  this->recovery_restore_pending_ = true;
  this->recovery_marker_ = marker;
  this->recovery_active_ = true;
  this->reset_series_();
  this->init_preferences_();
  if (!this->restore_()) return this->block_recovery_();
  this->recovery_blocked_ = false;
  this->recovery_restore_pending_ = false;
  this->reset_save_retry_();
  return true;
}

/** Archive only after a new confirmed clock proves a substantial rollback. */
bool RoomAggregateHistory::confirm_time(uint32_t epoch, uint16_t year,
                                        uint16_t day_of_year, uint8_t hour,
                                        bool synchronized) {
  using namespace usp_room_clock_recovery;
  if (!synchronized) return false;
  const uint16_t days = is_leap_year(year) ? 366 : 365;
  if (year < 1970 || day_of_year == 0 || day_of_year > days) return false;
  if (this->recovery_blocked_ && !this->settle_recovery_(true)) return false;
  const uint32_t day = local_day_key(year, day_of_year);
  const Decision decision = classify(this->stored_extent_(),
                                    {epoch, day, hour, true});
  if (decision == Decision::KEEP_SERIES) {
    const bool was_full = this->archive_full_;
    this->archive_full_ = false;
    if (was_full && !this->save_failed_) this->reset_save_retry_();
    return true;
  }
  if (decision != Decision::ARCHIVE_FUTURE) return false;
  const uint32_t cutoff_hour = day * 24U + hour;
  if (this->recovery_active_ &&
      usp_room_recovery_store::active_bank(this->recovery_marker_) >= 2U) {
    if (!this->archive_full_)
      ESP_LOGW(TAG, "Recovery archive is full; all saved series retained");
    this->archive_full_ = true;
    this->storage_ok_ = false;
    this->status_set_warning("Room history archive capacity reached");
    return false;
  }
  if (!this->flush()) return false;
  this->recovery_blocked_ = true;
  if (!this->recovery_active_) {
    usp_room_recovery_store::Marker marker{};
    marker.cutoff_day = day;
    marker.cutoff_hour = cutoff_hour;
    usp_room_recovery_store::save_once(marker);
    return this->settle_recovery_(false);
  }
  const auto previous = this->recovery_marker_;
  usp_room_recovery_store::select_bank2_once(previous, cutoff_hour, day);
  if (!this->settle_recovery_(false)) return false;
  return this->recovery_marker_.version == usp_room_recovery_store::VERSION_V2 &&
         usp_room_recovery_store::selected_cutoff_hour(this->recovery_marker_) == cutoff_hour &&
         usp_room_recovery_store::selected_cutoff_day(this->recovery_marker_) == day;
}

/** Allocate lazily only when the archive contains an eligible measurement. */
bool RoomAggregateHistory::ensure_restore_storage_() {
  if (this->active_) return true;
  if (!this->allocate_storage_()) return false;
  this->active_ = true;
  return true;
}

/** Read one archived hour page without allowing its future cursor to escape. */
bool RoomAggregateHistory::restore_archived_hour_page_(
    uint32_t page_id, uint32_t bank, uint32_t cutoff_hour) {
  HourPage page{};
  bool corrupt = false;
  const auto preference = archived_preference(
      true, page_id % HOUR_PAGE_SLOTS, bank);
  if (!load_compatible<HourPage, LegacyHourPage>(
          preference, page, valid_hour_page, valid_legacy_hour, corrupt)) {
    if (corrupt) ++this->corrupt_pages_;
    return true;
  }
  if (page.page_id != page_id) return true;
  for (const auto &point : page.points) {
    if (!aggregate_nonempty(point) || point.bucket > cutoff_hour) continue;
    if (!this->ensure_restore_storage_()) return false;
    Aggregate *target = this->upsert_hour_(point.bucket);
    if (target != nullptr) *target = point;
  }
  return true;
}

/** Keep complete past days; a cutoff day is rebuilt only from retained hours. */
bool RoomAggregateHistory::restore_archived_day_page_(
    uint32_t page_id, uint32_t bank, uint32_t cutoff_day) {
  DayPage page{};
  bool corrupt = false;
  const auto preference = archived_preference(
      false, page_id % DAY_PAGE_SLOTS, bank);
  if (!load_compatible<DayPage, LegacyDayPage>(
          preference, page, valid_day_page, valid_legacy_day, corrupt)) {
    if (corrupt) ++this->corrupt_pages_;
    return true;
  }
  if (page.page_id != page_id) return true;
  for (const auto &point : page.points) {
    if (!aggregate_nonempty(point) || point.bucket >= cutoff_day) continue;
    if (!this->ensure_restore_storage_()) return false;
    Aggregate *target = this->upsert_day_(point.bucket);
    if (target != nullptr) *target = point;
  }
  return true;
}

/** Rebuild one partial day exactly once from the currently retained hours. */
bool RoomAggregateHistory::rebuild_archived_day_(uint32_t cutoff_day) {
  Aggregate rebuilt{};
  rebuilt.bucket = cutoff_day;
  bool found = false;
  for (size_t index = 0; index < this->hourly_count_; ++index) {
    const Aggregate *hour = this->hour_at_(index);
    if (hour->bucket / 24U != cutoff_day) continue;
    if (!this->merge_aggregate_(&rebuilt, *hour)) return false;
    found = true;
  }
  if (!found) return true;
  Aggregate *day = this->upsert_day_(cutoff_day);
  if (day == nullptr) return false;
  *day = rebuilt;
  return true;
}

/** Restore one immutable archive with its effective cutoff. */
bool RoomAggregateHistory::restore_archive_bank_(
    uint32_t bank, uint32_t cutoff_hour, uint32_t cutoff_day) {
  const uint32_t last_hour_page = cutoff_hour / 24U;
  const uint32_t first_hour_page = last_hour_page >= HOUR_PAGE_SLOTS - 1U
      ? last_hour_page - HOUR_PAGE_SLOTS + 1U : 0U;
  for (uint32_t page = first_hour_page; page <= last_hour_page; ++page) {
    if (!this->restore_archived_hour_page_(page, bank, cutoff_hour)) return false;
  }
  const uint32_t last_day_page = cutoff_day / 31U;
  const uint32_t first_day_page = last_day_page >= DAY_PAGE_SLOTS - 1U
      ? last_day_page - DAY_PAGE_SLOTS + 1U : 0U;
  for (uint32_t page = first_day_page; page <= last_day_page; ++page) {
    if (!this->restore_archived_day_page_(page, bank, cutoff_day)) return false;
  }
  return this->rebuild_archived_day_(cutoff_day);
}

/** Restore archives oldest-to-newest; every later cutoff also limits older data. */
bool RoomAggregateHistory::restore_archive_() {
  const auto &marker = this->recovery_marker_;
  uint32_t first_hour = marker.cutoff_hour;
  uint32_t first_day = marker.cutoff_day;
  if (marker.version == usp_room_recovery_store::VERSION_V2) {
    first_hour = std::min(first_hour, marker.second_cutoff_hour);
    first_day = std::min(first_day, marker.second_cutoff_day);
  }
  if (!this->restore_archive_bank_(0U, first_hour, first_day)) return false;
  if (marker.version == usp_room_recovery_store::VERSION_V2 &&
      !this->restore_archive_bank_(1U, marker.second_cutoff_hour,
                                   marker.second_cutoff_day)) return false;
  this->finish_archive_();
  return true;
}

/** Combine retained hourly totals without inventing samples or timestamps. */
bool RoomAggregateHistory::merge_aggregate_(Aggregate *target,
                                           const Aggregate &source) {
  const uint32_t valid = target->valid_count + source.valid_count;
  const uint32_t gaps = target->gap_count + source.gap_count;
  if (valid + gaps > 300U) return false;
  if (source.valid_count != 0) {
    if (target->valid_count == 0) {
      target->temperature_min_centi = source.temperature_min_centi;
      target->temperature_max_centi = source.temperature_max_centi;
      target->humidity_min_centi = source.humidity_min_centi;
      target->humidity_max_centi = source.humidity_max_centi;
    } else {
      target->temperature_min_centi = std::min(target->temperature_min_centi, source.temperature_min_centi);
      target->temperature_max_centi = std::max(target->temperature_max_centi, source.temperature_max_centi);
      target->humidity_min_centi = std::min(target->humidity_min_centi, source.humidity_min_centi);
      target->humidity_max_centi = std::max(target->humidity_max_centi, source.humidity_max_centi);
    }
  }
  target->temperature_sum_centi += source.temperature_sum_centi;
  target->humidity_sum_centi += source.humidity_sum_centi;
  target->valid_count = static_cast<uint16_t>(valid);
  target->gap_count = static_cast<uint16_t>(gaps);
  return true;
}

/** Unknown partial intervals keep the existing conservative legacy fences. */
void RoomAggregateHistory::finish_archive_() {
  const Aggregate *hour = this->hour_at_(this->hourly_count_ - 1U);
  const Aggregate *day = this->day_at_(this->daily_count_ - 1U);
  if (hour != nullptr) {
    this->legacy_hour_fence_ = hour->bucket;
    this->have_legacy_hour_fence_ = true;
    this->current_hour_page_id_ = hour->bucket / 24U;
    this->hour_page_known_ = true;
  }
  if (day != nullptr) {
    this->legacy_day_fence_ = day->bucket;
    this->have_legacy_day_fence_ = true;
    this->current_day_page_id_ = day->bucket / 31U;
    this->day_page_known_ = true;
  }
  this->loaded_from_nvs_ = hour != nullptr || day != nullptr;
  this->last_checkpoint_ms_ = millis();
}

}  // namespace esphome::usp_room_aggregate_history
