#pragma once

#include "esphome/core/automation.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "usp_http_retry_after.h"

#include <ArduinoJson.h>
#include "esp_http_client.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome::usp_outage {

enum class OutageWireSchema : uint8_t {
  PLANNED = 0,
  PROBABLE = 1,
};

class OutageClient : public Component {
 public:
  static constexpr size_t MAX_DAY_SLOTS = 8;
  static constexpr size_t WEEK_DAYS = 7;

  enum class SlotType : uint8_t {
    UNKNOWN = 0,
    NOT_PLANNED = 1,
    DEFINITE = 2,
  };

  enum class DayStatus : uint8_t {
    UNKNOWN = 0,
    WAITING = 1,
    SCHEDULE_APPLIES = 2,
    EMERGENCY = 3,
    NO_OUTAGES = 4,
  };

  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_refresh_interval(uint32_t value) { refresh_interval_ms_ = value; }
  void set_stale_after(uint32_t value) { stale_after_ms_ = value; }
  void set_initial_delay(uint32_t value) { initial_delay_ms_ = value; }

  /** Use the same local clock as the panel, without separate time settings. */
  void set_time_source(time::RealTimeClock *value) { time_source_ = value; }

  Trigger<> *get_update_trigger() { return &update_trigger_; }

  bool set_group(int32_t group, int32_t subgroup = 1);
  void request_refresh();
  void dump_runtime() const;
  bool qa_force_cached_state();
  bool qa_set_age_seconds(uint32_t age_seconds);

  bool configured() const;
  uint8_t group_number() const;
  uint8_t subgroup_number() const;
  std::string group_label() const;
  bool schedule_valid() const;
  bool tomorrow_valid() const;
  bool schedule_stale() const;
  bool last_fetch_ok() const;
  bool probable_valid() const;
  uint32_t sequence() const;
  std::string provider_updated() const;

  uint8_t today_status() const;
  uint8_t tomorrow_status() const;
  std::string today_status_text(uint8_t language) const;
  std::string tomorrow_status_text(uint8_t language) const;
  std::string today_date() const;
  std::string tomorrow_date() const;

  size_t today_slot_count() const;
  uint16_t today_slot_start(size_t index) const;
  uint16_t today_slot_end(size_t index) const;
  uint8_t today_slot_type(size_t index) const;
  uint16_t today_outage_minutes() const;
  uint16_t tomorrow_outage_minutes() const;
  int16_t next_outage_start(int16_t current_minute) const;
  int16_t next_outage_end(int16_t current_minute) const;
  uint16_t week_outage_minutes(size_t day) const;

 protected:
  struct OutageSlot {
    uint16_t start_min{0};
    uint16_t end_min{0};
    SlotType type{SlotType::UNKNOWN};
  };

  struct DaySchedule {
    std::string date;
    DayStatus status{DayStatus::UNKNOWN};
    size_t count{0};
    std::array<OutageSlot, MAX_DAY_SLOTS> slots{};
  };

  struct OutageSnapshot {
    bool valid{false};
    bool fetch_ok{false};
    bool probable_valid{false};
    uint8_t group{0};
    uint32_t sequence{0};
    uint32_t received_ms{0};
    std::string provider_updated;
    DaySchedule today{};
    DaySchedule tomorrow{};
    std::array<uint16_t, WEEK_DAYS> week_outage_minutes{};
  };

  struct StoredGroup {
    uint32_t magic{0};
    uint8_t group{0};
    uint8_t subgroup{1};
    uint8_t reserved[2]{};
  };

  static void worker_entry(void *argument);
  void worker_loop();
  void process_refresh_request(bool scheduled);

  bool fetch_group_json_(
      esp_http_client_handle_t client, const std::string &url,
      OutageWireSchema schema, uint8_t group, uint8_t subgroup,
      std::string *object);
  bool parse_planned_(
      const std::string &object, OutageSnapshot *snapshot);
  bool parse_probable_(
      const std::string &object, OutageSnapshot *snapshot);
  bool parse_day_(JsonObjectConst object, DaySchedule *schedule) const;
  void parse_week_(JsonObjectConst slots, OutageSnapshot *snapshot) const;
  static SlotType parse_slot_type_(const std::string &value);
  static DayStatus parse_day_status_(const std::string &value);
  static std::string status_text_(DayStatus status, uint8_t language);
  static uint16_t outage_minutes_(const DaySchedule &schedule);

  void load_group_preference_();
  bool save_group_preference_();
  uint8_t selected_group_() const;
  uint8_t selected_subgroup_() const;
  bool day_current_(const DaySchedule &day, bool tomorrow) const;
  void check_calendar_();
  void dump_calendar_() const;
  void clear_snapshot_();
  void schedule_retry_();
  void schedule_success_();
  void log_snapshot_() const;

  std::string planned_url_() const;
  std::string probable_url_() const;

  static constexpr uint32_t GROUP_MAGIC = 0x4F555431;
  static constexpr uint32_t GROUP_PREF_KEY =
      fnv1a_hash("usp_outage_group_v1");
  static constexpr uint32_t MIN_RETRY_MS = 30000;
  static constexpr uint32_t MAX_RETRY_MS = 900000;

  mutable std::mutex state_mutex_;
  mutable std::mutex group_mutex_;
  OutageSnapshot snapshot_{};
  StoredGroup stored_group_{};
  ESPPreferenceObject group_pref_{};
  TaskHandle_t worker_task_{nullptr};

  std::atomic<bool> stop_worker_{false};
  std::atomic<bool> refresh_requested_{false};
  std::atomic<bool> update_pending_{false};

  uint32_t refresh_interval_ms_{900000};
  uint32_t stale_after_ms_{2700000};
  uint32_t initial_delay_ms_{18000};
  uint32_t next_refresh_ms_{0};
  uint32_t retry_delay_ms_{MIN_RETRY_MS};
  usp_http_retry_after::Gate retry_after_gate_{};
  bool last_stale_state_{true};
  time::RealTimeClock *time_source_{nullptr};
  uint32_t calendar_date_{0};
  uint32_t last_valid_calendar_date_{0};
  uint32_t last_calendar_check_ms_{0};
  bool calendar_checked_{false};

  Trigger<> update_trigger_;
};

}  // namespace esphome::usp_outage
