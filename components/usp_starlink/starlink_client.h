#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome::usp_starlink {

class StarlinkClient : public Component {
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
  void request_probe();
  void request_gps_inhibit(bool inhibit);

  Trigger<> *get_update_trigger() { return &update_trigger_; }

  bool enabled() const { return enabled_.load(std::memory_order_acquire); }
  bool configured() const;
  bool reachable() const;
  bool gps_status_valid() const;
  bool gps_valid() const;
  bool inhibit_gps() const;
  uint32_t gps_satellites() const;
  int grpc_status() const;
  uint32_t latency_ms() const;
  uint32_t error_count() const;
  uint32_t sequence() const;
  std::string host() const;
  uint16_t port() const { return port_.load(std::memory_order_acquire); }

 protected:
  struct Snapshot {
    bool valid{false};
    bool reachable{false};
    bool gps_status_valid{false};
    bool gps_valid{false};
    bool inhibit_gps{false};
    uint32_t gps_satellites{0};
    int grpc_status{-1};
    uint32_t latency_ms{0};
    uint32_t errors{0};
    uint32_t sequence{0};
  };

  static void worker_entry(void *argument);
  void worker_loop();
  void perform_probe_(bool scheduled);
  void perform_gps_inhibit_(bool inhibit);
  std::string host_copy_() const;
  void commit_result_(bool reachable, uint32_t latency_ms,
                      bool gps_status_valid, bool gps_valid,
                      bool inhibit_gps, uint32_t gps_satellites,
                      int grpc_status);
  void commit_command_error_(int grpc_status, uint32_t latency_ms);

  mutable std::mutex host_mutex_;
  mutable std::mutex state_mutex_;
  std::string host_{"192.168.100.1"};
  Snapshot snapshot_{};

  TaskHandle_t worker_task_{nullptr};
  std::atomic<bool> stop_worker_{false};
  std::atomic<bool> enabled_{false};
  std::atomic<bool> probe_requested_{false};
  std::atomic<int> gps_inhibit_requested_{-1};
  std::atomic<bool> update_pending_{false};
  std::atomic<uint16_t> port_{9200};

  uint32_t refresh_interval_ms_{30000};
  uint32_t initial_delay_ms_{10000};
  uint32_t connect_timeout_ms_{1500};
  uint32_t next_refresh_ms_{0};
  Trigger<> update_trigger_;
};

}  // namespace esphome::usp_starlink
