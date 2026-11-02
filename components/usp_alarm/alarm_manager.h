#pragma once
#include "alarm_schedule.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/time/real_time_clock.h"
#include <functional>
#include <string>

namespace esphome::usp_alarm {

class AlarmManager final : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void set_clock(time::RealTimeClock *clock) { clock_ = clock; }
  void set_trusted(std::function<bool()> trusted) {
    trusted_ = std::move(trusted);
  }
  bool configure(size_t index, const std::string &time, uint8_t days,
                 bool enabled, uint32_t revision = UINT32_MAX);
  void dismiss();
  void dump_runtime() const;
  uint32_t revision(size_t index) const { return revisions_[index]; }
  uint8_t error_code() const { return error_code_; }
  const ::usp_alarm_schedule::Record &slot(size_t index) const {
    return schedule_.store().slots[index];
  }
  std::string time_text(size_t index) const;
  std::string notification_time(size_t index) const;
  uint8_t active() const { return schedule_.active(); }
  bool clock_valid() const { return clock_valid_; }
  bool storage_ok() const { return storage_ok_; }
  Trigger<uint8_t> *get_alarm_trigger() { return &alarm_trigger_; }
  Trigger<> *get_update_trigger() { return &update_trigger_; }

 private:
  bool persist_(const ::usp_alarm_schedule::Store &store);
  void queue_firing_guard_retry_();
  void retry_firing_guard_();
  ::usp_alarm_schedule::Schedule schedule_{};
  ESPPreferenceObject preference_{};
  time::RealTimeClock *clock_{nullptr};
  std::function<bool()> trusted_{};
  Trigger<uint8_t> alarm_trigger_{};
  Trigger<> update_trigger_{};
  std::array<uint32_t, ::usp_alarm_schedule::SLOT_COUNT> revisions_{};
  static constexpr uint32_t STORAGE_RETRY_MS = 30000;

  ::usp_alarm_schedule::Store pending_firing_store_{};
  uint32_t storage_retry_at_{0};
  uint8_t error_code_{0};
  bool clock_valid_{false};
  bool storage_ok_{true};
  bool firing_retry_pending_{false};
};

}  // namespace esphome::usp_alarm
