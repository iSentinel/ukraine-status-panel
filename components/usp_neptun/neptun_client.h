#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "neptun_alert_parser.h"
#include "neptun_threat_parser.h"
#include "usp_http_retry_after.h"

#include <atomic>
#include <cstdint>
#include <mutex>

#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome::usp_neptun {

class NeptunClient : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_refresh_interval(uint32_t value) { refresh_interval_ms_ = value; }
  void set_idle_refresh_interval(uint32_t value) {
    idle_refresh_interval_ms_ = value;
  }
  void set_stale_after(uint32_t value) { stale_after_ms_ = value; }
  void set_initial_delay(uint32_t value) { initial_delay_ms_ = value; }

  Trigger<> *get_update_trigger() { return &update_trigger_; }

  void request_refresh();
  void dump_runtime() const;

  bool valid() const;
  bool stale() const;
  bool last_fetch_ok() const;
  uint16_t total() const;
  uint16_t uav() const;
  uint16_t fpv() const;
  uint16_t recon() const;
  uint16_t missile() const;
  uint16_t ballistic() const;
  uint16_t kab() const;
  uint16_t mig31k() const;
  uint16_t unknown() const;
  ThreatCounts threat_counts() const;
  uint32_t sequence() const;
  uint32_t body_bytes() const;
  ThreatCounts region_threat_counts(uint16_t region_id) const;
  uint16_t threat_unknown_region_count() const;
  void dump_region_threats(uint16_t region_id) const;
  bool alerts_valid() const;
  bool alerts_stale() const;
  bool alerts_last_fetch_ok() const;
  uint8_t region_alert_level(uint16_t region_id) const;
  uint16_t alert_entry_count() const;
  uint16_t alert_unknown_region_count() const;
  uint16_t alert_unknown_level_count() const;
  uint32_t alerts_body_bytes() const;
  void dump_region_alert(uint16_t region_id) const;

 public:
  struct Snapshot {
    bool valid{false};
    bool fetch_ok{false};
    uint32_t received_ms{0};
    uint32_t sequence{0};
    uint32_t body_bytes{0};
    bool alerts_valid{false};
    bool alerts_fetch_ok{false};
    uint32_t alerts_received_ms{0};
    uint32_t alerts_body_bytes{0};
    uint16_t alert_entries{0};
    uint16_t alert_unknown_regions{0};
    uint16_t alert_unknown_levels{0};
    std::array<uint8_t, REGION_ALERT_COUNT> alert_levels{};
    // Preserve the existing polling cadence independently of visible counts.
    uint16_t threat_entries{0};
    uint16_t total{0};
    uint16_t uav{0};
    uint16_t fpv{0};
    uint16_t recon{0};
    uint16_t missile{0};
    uint16_t ballistic{0};
    uint16_t kab{0};
    uint16_t mig31k{0};
    uint16_t unknown{0};
    uint16_t threat_unknown_regions{0};
    std::array<ThreatCounts, REGION_ALERT_COUNT> region_threats{};
    std::array<ThreatCounts, REGION_ALERT_COUNT> active_region_threats{};
    uint32_t threat_server_epoch{0};
    uint16_t threat_point_candidates{0};
    uint16_t threat_point_count{0};
    uint16_t threat_point_dropped_capacity{0};
    std::array<ThreatPoint, THREAT_POINT_CAPACITY> threat_points{};
  };

  Snapshot snapshot() const { return this->snapshot_copy_(); }
  uint32_t stale_after_ms() const { return this->stale_after_ms_; }

 protected:
  static void worker_entry(void *argument);
  void worker_loop();
  void process_request(bool scheduled);
  bool fetch_snapshot_(Snapshot *snapshot);
  bool fetch_threats_(Snapshot *snapshot);
  bool fetch_alerts_(Snapshot *snapshot);
  bool ensure_http_client_();
  void reset_http_client_();
  void schedule_success_();
  void schedule_retry_();
  void schedule_gate_retry_();
  void log_snapshot_() const;
  Snapshot snapshot_copy_() const;

  static constexpr uint32_t MIN_RETRY_MS = 15000;
  static constexpr uint32_t MAX_RETRY_MS = 120000;
  static constexpr uint32_t GATE_BUSY_RETRY_MS = 2000;

  mutable std::mutex state_mutex_;
  Snapshot snapshot_{};
  TaskHandle_t worker_task_{nullptr};
  esp_http_client_handle_t http_client_{nullptr};
  uint32_t transport_connections_{0};

  std::atomic<bool> stop_worker_{false};
  std::atomic<bool> refresh_requested_{false};
  std::atomic<bool> update_pending_{false};

  uint32_t refresh_interval_ms_{20000};
  uint32_t idle_refresh_interval_ms_{45000};
  uint32_t stale_after_ms_{120000};
  uint32_t initial_delay_ms_{24000};
  uint32_t next_refresh_ms_{0};
  uint32_t retry_delay_ms_{MIN_RETRY_MS};
  bool https_gate_busy_during_request_{false};
  usp_http_retry_after::Gate retry_after_gate_{};
  bool last_stale_state_{true};

  Trigger<> update_trigger_;
};

}  // namespace esphome::usp_neptun
