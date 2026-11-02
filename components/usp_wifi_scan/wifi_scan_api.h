#pragma once

#include "esphome/components/web_server_idf/web_server_idf.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace esphome::usp_wifi_scan {

struct WifiScanEntry {
  char ssid[33];
  int8_t rssi;
  uint8_t channel;
  bool secure;
};

enum class WifiScanState : uint8_t {
  IDLE,
  START_PENDING,
  SCANNING,
  READY,
  ERROR,
};

class WifiScanApi final : public Component,
                          public web_server_idf::AsyncWebHandler,
                          public wifi::WiFiScanResultsListener {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  bool canHandle(web_server_idf::AsyncWebServerRequest *request) const override;
  void handleRequest(web_server_idf::AsyncWebServerRequest *request) override;
  bool isRequestHandlerTrivial() const override { return false; }
  void on_wifi_scan_results(const wifi::wifi_scan_vector_t<wifi::WiFiScanResult> &results) override;

 protected:
  static constexpr size_t MAX_NETWORKS = 24;
  static constexpr size_t RESPONSE_BYTES = 6144;
  static_assert(2 * sizeof(WifiScanEntry) * MAX_NETWORKS + RESPONSE_BYTES <= 8192,
                "Wi-Fi scan buffers must stay within 8 KiB");
  static constexpr uint32_t SCAN_TIMEOUT_MS = 15000;
  static constexpr uint32_t SCAN_DEFER_MS = 250;

  bool route_is_(web_server_idf::AsyncWebServerRequest *request) const;
  bool start_scan_();
  bool update_scan_lease_();
  void consume_radio_failure_();
  void start_radio_scan_();
  bool request_high_performance_();
  void release_high_performance_();
  void collect_results_(const wifi::wifi_scan_vector_t<wifi::WiFiScanResult> &results,
                        WifiScanEntry *records, uint16_t &visible, uint16_t &hidden);
  bool publish_results_();
  uint16_t deduplicate_(WifiScanEntry *records, uint16_t count);
  void sort_by_rssi_(WifiScanEntry *records, uint16_t count);
  bool build_json_(uint16_t visible, uint16_t hidden, size_t *length);
  bool append_(size_t *offset, const char *text);
  bool append_ssid_(size_t *offset, const char *ssid);
  void send_state_(web_server_idf::AsyncWebServerRequest *request, int code,
                   const char *state) const;
  void send_error_(web_server_idf::AsyncWebServerRequest *request,
                   int code, const char *error) const;
  bool lock_(TickType_t wait_ticks = pdMS_TO_TICKS(100)) const;
  void unlock_() const;
  void expire_scan_();
  void expire_scan_locked_();

  RAMUniquePtr<WifiScanEntry[]> records_;
  // Only the main-task listener and loop access this staging storage.
  RAMUniquePtr<WifiScanEntry[]> pending_records_;
  uint16_t pending_scan_count_{0};
  uint16_t pending_hidden_count_{0};
  bool scan_results_pending_{false};
  RAMUniquePtr<char[]> response_;
  mutable StaticSemaphore_t mutex_storage_{};
  mutable SemaphoreHandle_t mutex_{nullptr};
  WifiScanState state_{WifiScanState::IDLE};
  uint32_t scan_requested_ms_{0};
  uint32_t scan_started_ms_{0};
  uint16_t scan_count_{0};
  uint16_t hidden_count_{0};
  bool route_registered_{false};
  bool high_performance_requested_{false};
  // HTTP changes the generation only while holding the state mutex.
  uint32_t scan_generation_{0};
  // Lease ownership and driver failure are accessed only by the main loop.
  uint32_t prepared_generation_{0}, radio_generation_{0};
  bool lease_prepared_{false}, radio_start_failed_{false};
};

}  // namespace esphome::usp_wifi_scan
