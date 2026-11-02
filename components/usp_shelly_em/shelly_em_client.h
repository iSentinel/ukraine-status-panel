#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome::usp_shelly_em {

struct Em1Channel {
  bool valid{false};
  bool meter_error{false};
  float voltage{NAN};
  float current{NAN};
  float act_power{NAN};
  float aprt_power{NAN};
  float pf{NAN};
  float freq{NAN};
};

class ShellyEmClient : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_refresh_interval(uint32_t value) { refresh_interval_ms_ = value; }
  void set_initial_delay(uint32_t value) { initial_delay_ms_ = value; }
  void set_connect_timeout(uint32_t value) { connect_timeout_ms_ = value; }
  void set_enabled(bool value);
  void set_host(const std::string &value);
  void set_port(uint16_t value);
  void set_password(const std::string &value);
  void request_refresh();

  Trigger<> *get_update_trigger() { return &update_trigger_; }

  bool enabled() const { return enabled_.load(std::memory_order_acquire); }
  bool configured() const;
  bool reachable() const;
  bool complete() const;
  bool auth_required() const;
  Em1Channel channel(uint8_t index) const;
  int last_code() const;
  uint32_t last_duration_ms() const;
  uint32_t error_count() const;
  uint32_t sequence() const;
  std::string host() const;
  uint16_t port() const { return port_.load(std::memory_order_acquire); }

 protected:
  struct Snapshot {
    bool reachable{false};
    bool complete{false};
    bool auth_required{false};
    std::array<Em1Channel, 2> channels{};
    int last_code{0};
    uint32_t duration_ms{0};
    uint32_t errors{0};
    uint32_t sequence{0};
  };

  static void worker_entry(void *argument);
  void worker_loop();
  void perform_refresh_(bool scheduled);
  bool fetch_channel_(uint8_t index, Em1Channel *channel, int *code,
                      uint32_t *duration_ms, bool *auth_required);
  std::string host_copy_() const;
  std::string password_copy_() const;
  void commit_(const Snapshot &next);

  mutable std::mutex config_mutex_;
  mutable std::mutex state_mutex_;
  std::string host_;
  std::string password_;
  Snapshot snapshot_{};

  TaskHandle_t worker_task_{nullptr};
  std::atomic<bool> stop_worker_{false};
  std::atomic<bool> enabled_{false};
  std::atomic<bool> refresh_requested_{false};
  std::atomic<bool> update_pending_{false};
  std::atomic<uint16_t> port_{80};

  uint32_t refresh_interval_ms_{10000};
  uint32_t initial_delay_ms_{8000};
  uint32_t connect_timeout_ms_{1800};
  uint32_t next_refresh_ms_{0};
  Trigger<> update_trigger_;
};

}  // namespace esphome::usp_shelly_em
