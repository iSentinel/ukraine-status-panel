#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "usp_http_retry_after.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome::usp_svitlobot {

class SvitloBotClient : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_refresh_interval(uint32_t value) { refresh_interval_ms_ = value; }
  void set_initial_delay(uint32_t value) { initial_delay_ms_ = value; }
  void set_enabled(bool value);
  void set_key(const std::string &value);
  void request_ping();

  Trigger<> *get_update_trigger() { return &update_trigger_; }

  bool enabled() const { return enabled_.load(std::memory_order_acquire); }
  bool configured() const;
  bool last_ok() const;
  int last_code() const;
  uint32_t last_duration_ms() const;
  uint32_t error_count() const;
  uint32_t sequence() const;

 protected:
  struct Snapshot {
    bool valid{false};
    bool ok{false};
    int code{0};
    uint32_t duration_ms{0};
    uint32_t errors{0};
    uint32_t sequence{0};
  };

  static void worker_entry(void *argument);
  void worker_loop();
  void perform_ping_(bool scheduled);
  std::string key_copy_() const;
  void commit_result_(bool ok, int code, uint32_t duration_ms);

  mutable std::mutex key_mutex_;
  mutable std::mutex state_mutex_;
  std::string key_;
  Snapshot snapshot_{};

  TaskHandle_t worker_task_{nullptr};
  std::atomic<bool> stop_worker_{false};
  std::atomic<bool> enabled_{false};
  std::atomic<bool> ping_requested_{false};
  std::atomic<bool> update_pending_{false};

  uint32_t refresh_interval_ms_{60000};
  uint32_t initial_delay_ms_{15000};
  uint32_t next_refresh_ms_{0};
  usp_http_retry_after::Gate retry_after_gate_{};
  Trigger<> update_trigger_;
};

}  // namespace esphome::usp_svitlobot
