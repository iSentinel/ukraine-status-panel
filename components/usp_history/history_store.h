#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome::usp_history {

static constexpr size_t HISTORY_CAPACITY = 32;

enum class EventType : uint8_t {
  ENERGY = 1,
  THREAT = 2,
  SCHEDULE = 3,
};

struct HistoryEvent {
  uint32_t epoch{0};
  uint16_t region{0};
  int16_t value{0};
  int16_t aux{0};
  uint8_t type{0};
  // For SCHEDULE: 1 or 2 is the known subgroup; 0 is legacy/unknown.
  uint8_t flags{0};
};

struct HistoryBlob {
  uint32_t magic{0};
  uint16_t version{0};
  uint16_t count{0};
  uint16_t head{0};
  uint16_t reserved{0};
  uint32_t total_events{0};
  HistoryEvent events[HISTORY_CAPACITY]{};
  uint32_t checksum{0};
};

static_assert(sizeof(HistoryEvent) == 12, "HistoryEvent layout changed");
static_assert(sizeof(HistoryBlob) == 404, "HistoryBlob layout changed");

class HistoryStore : public Component {
 public:
  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_time(time::RealTimeClock *clock) { this->clock_ = clock; }
  void set_commit_delay(uint32_t delay_ms) { this->commit_delay_ms_ = delay_ms; }
  Trigger<> *get_update_trigger() { return &this->update_trigger_; }

  bool record_energy(uint16_t region, uint8_t status);
  bool record_threat(uint16_t region, uint8_t state, uint16_t flags);
  /** Zero subgroup marks a legacy record whose subgroup was never stored. */
  bool record_schedule(uint8_t group, int today_minutes, int tomorrow_minutes,
                       uint8_t subgroup = 0);
  /** Read the owning group label once, retaining its selected subgroup. */
  bool record_schedule(const std::string &group, int today_minutes,
                       int tomorrow_minutes);
  bool qa_record(uint8_t kind, uint16_t region, int value, int aux);
  void qa_mark_dirty_for_shutdown_probe();

  void clear();
  bool flush();
  void dump_runtime() const;

  size_t count() const { return this->blob_.count; }
  uint32_t total_events() const { return this->blob_.total_events; }
  bool storage_ok() const { return this->storage_ok_; }
  bool loaded_from_nvs() const { return this->loaded_from_nvs_; }
  uint32_t save_count() const { return this->save_count_; }

  std::string count_text(uint8_t language) const;
  std::string event_summary(size_t newest_index, uint8_t language) const;
  std::string event_summary_compact(size_t newest_index, uint8_t language) const;
  std::string quality_text(uint8_t language) const;
  std::string quality_detail(uint8_t language) const;

 protected:
  bool record_(EventType type, uint16_t region, int value, int aux,
               uint8_t flags);
  bool is_duplicate_(const HistoryEvent &event) const;
  const HistoryEvent *event_at_(size_t newest_index) const;
  const HistoryEvent *latest_() const;

  void reset_blob_();
  bool load_();
  bool save_(bool notify = true);
  void mark_dirty_();
  bool retry_due_(uint32_t now) const;
  void note_save_failure_(uint32_t now);
  void reset_save_retry_();
  uint32_t now_epoch_() const;

  static uint32_t checksum_(const HistoryBlob &blob);
  static bool valid_blob_(const HistoryBlob &blob);
  static std::string time_prefix_(uint32_t epoch);
  static std::string energy_text_(int value, uint8_t language);
  static std::string threat_text_(int value, uint8_t language);
  static std::string schedule_group_text_(const HistoryEvent &event,
                                          uint8_t language);
  static std::string schedule_text_(const HistoryEvent &event,
                                    uint8_t language);
  static std::string compact_duration_(int minutes);
  static std::string schedule_text_compact_(const HistoryEvent &event,
                                            uint8_t language);
  static std::string event_body_(const HistoryEvent &event,
                                 uint8_t language);

  time::RealTimeClock *clock_{nullptr};
  ESPPreferenceObject preference_{};
  HistoryBlob blob_{};
  Trigger<> update_trigger_{};
  uint32_t commit_delay_ms_{5000};
  static constexpr uint32_t MIN_RETRY_MS = 30000;
  static constexpr uint32_t MAX_RETRY_MS = 300000;

  uint32_t dirty_since_ms_{0};
  uint32_t save_count_{0};
  uint32_t last_save_attempt_ms_{0};
  uint32_t retry_delay_ms_{MIN_RETRY_MS};
  bool dirty_{false};
  bool storage_ok_{true};
  bool loaded_from_nvs_{false};
  bool save_failed_{false};
};

}  // namespace esphome::usp_history
