#include "wifi_scan_api.h"

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace esphome::usp_wifi_scan {
namespace {
static constexpr char TAG[] = "usp.wifi.scan";
static constexpr char SCAN_PATH[] = "/usp/network/scan";
}

void WifiScanApi::setup() {
  ExternalRAMAllocator<WifiScanEntry> record_allocator;
  ExternalRAMAllocator<char> response_allocator;
  ExternalRAMAllocator<WifiScanEntry> pending_allocator(
      ExternalRAMAllocator<WifiScanEntry>::ALLOC_EXTERNAL);
  this->records_ = record_allocator.make_unique_array_for_overwrite(MAX_NETWORKS);
  this->pending_records_ = pending_allocator.make_unique_array_for_overwrite(MAX_NETWORKS);
  this->response_ = response_allocator.make_unique_array_for_overwrite(RESPONSE_BYTES);
  this->mutex_ = xSemaphoreCreateMutexStatic(&this->mutex_storage_);
  if (!this->records_ || !this->pending_records_ || !this->response_ || this->mutex_ == nullptr) {
    ESP_LOGE(TAG, "Wi-Fi scan buffers unavailable");
    this->mark_failed();
    return;
  }
  if (wifi::global_wifi_component != nullptr)
    wifi::global_wifi_component->add_scan_results_listener(this);
#ifdef USE_WEBSERVER_AUTH
  if (web_server_base::global_web_server_base != nullptr) {
    web_server_base::global_web_server_base->add_handler(this);
    this->route_registered_ = true;
    ESP_LOGI(TAG, "Authenticated non-blocking Wi-Fi scan API registered");
  }
#endif
}

void WifiScanApi::loop() {
  bool start_radio = false;
  uint16_t visible = 0, hidden = 0;
  if (!this->lock_(0)) return;
  this->consume_radio_failure_();
  this->expire_scan_locked_();
  const bool completed = this->publish_results_();
  if (completed) {
    visible = this->scan_count_;
    hidden = this->hidden_count_;
  }
  const bool prepared_now = this->update_scan_lease_();
  if (this->state_ == WifiScanState::START_PENDING && !prepared_now &&
      static_cast<uint32_t>(millis() - this->scan_requested_ms_) >= SCAN_DEFER_MS) {
    this->state_ = WifiScanState::SCANNING;
    this->scan_started_ms_ = millis();
    this->radio_generation_ = this->scan_generation_;
    start_radio = true;
  }
  this->unlock_();
  if (completed)
    ESP_LOGI(TAG, "Wi-Fi scan complete visible=%u hidden=%u", visible, hidden);
  if (start_radio) this->start_radio_scan_();
}

/** Move the main-task snapshot into HTTP-visible storage while locked. */
bool WifiScanApi::publish_results_() {
  if (!this->scan_results_pending_) return false;
  this->scan_results_pending_ = false;
  if (this->state_ != WifiScanState::SCANNING) return false;
  std::copy_n(this->pending_records_.get(), this->pending_scan_count_, this->records_.get());
  this->scan_count_ = this->pending_scan_count_;
  this->hidden_count_ = this->pending_hidden_count_;
  this->state_ = WifiScanState::READY;
  return true;
}

void WifiScanApi::dump_config() {
  ESP_LOGCONFIG(TAG, "Wi-Fi scan API");
  ESP_LOGCONFIG(TAG, "  Route: %s", SCAN_PATH);
  ESP_LOGCONFIG(TAG, "  Authenticated: %s", YESNO(this->route_registered_));
  ESP_LOGCONFIG(TAG, "  Non-blocking: YES");
  ESP_LOGCONFIG(TAG, "  Maximum visible networks: %u", static_cast<unsigned>(MAX_NETWORKS));
}

/** Let the main loop skip contention while retaining bounded HTTP waits. */
bool WifiScanApi::lock_(TickType_t wait_ticks) const {
  return this->mutex_ != nullptr && xSemaphoreTake(this->mutex_, wait_ticks) == pdTRUE;
}

void WifiScanApi::unlock_() const { xSemaphoreGive(this->mutex_); }

bool WifiScanApi::route_is_(web_server_idf::AsyncWebServerRequest *request) const {
  char buffer[web_server_idf::AsyncWebServerRequest::URL_BUF_SIZE];
  return request->url_to(buffer) == SCAN_PATH;
}

bool WifiScanApi::canHandle(web_server_idf::AsyncWebServerRequest *request) const {
  return this->route_is_(request);
}

void WifiScanApi::expire_scan_() {
  if (!this->lock_()) return;
  this->expire_scan_locked_();
  this->unlock_();
}

bool WifiScanApi::start_scan_() {
  this->expire_scan_();
  if (!this->lock_()) return false;
  if (this->state_ == WifiScanState::START_PENDING ||
      this->state_ == WifiScanState::SCANNING) {
    this->unlock_();
    return true;
  }
  this->state_ = WifiScanState::START_PENDING;
  ++this->scan_generation_;
  this->scan_requested_ms_ = millis();
  this->scan_count_ = 0;
  this->hidden_count_ = 0;
  this->unlock_();
  return true;
}

bool WifiScanApi::request_high_performance_() {
#ifdef USE_WIFI_RUNTIME_POWER_SAVE
  if (this->high_performance_requested_) return true;
  if (wifi::global_wifi_component == nullptr) return false;
  this->high_performance_requested_ =
      wifi::global_wifi_component->request_high_performance();
  if (!this->high_performance_requested_)
    ESP_LOGW(TAG, "Unable to request Wi-Fi high-performance mode");
  return this->high_performance_requested_;
#else
  return true;
#endif
}

void WifiScanApi::release_high_performance_() {
#ifdef USE_WIFI_RUNTIME_POWER_SAVE
  if (!this->high_performance_requested_) return;
  if (wifi::global_wifi_component != nullptr &&
      !wifi::global_wifi_component->release_high_performance())
    ESP_LOGW(TAG, "Unable to release Wi-Fi high-performance mode");
  this->high_performance_requested_ = false;
#endif
}

void WifiScanApi::start_radio_scan_() {
  wifi_scan_config_t config{};
  config.show_hidden = true;
  config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min = 15;
  config.scan_time.active.max = 40;
  config.home_chan_dwell_time = 150;
  config.coex_background_scan = true;
  const esp_err_t rc = esp_wifi_scan_start(&config, false);
  if (rc == ESP_OK) {
    ESP_LOGD(TAG, "Background Wi-Fi scan started");
    return;
  }
  // Publish the failure on the next loop; never wait for an HTTP owner.
  this->radio_start_failed_ = true;
  ESP_LOGW(TAG, "Wi-Fi scan start failed: %s", esp_err_to_name(rc));
}

/** Prepare a bounded result snapshot without touching the HTTP buffer. */
void WifiScanApi::collect_results_(
    const wifi::wifi_scan_vector_t<wifi::WiFiScanResult> &results,
    WifiScanEntry *records, uint16_t &visible, uint16_t &hidden) {
  uint16_t count = 0;
  hidden = 0;
  for (const auto &result : results) {
    if (result.get_is_hidden() || result.get_ssid().empty()) {
      hidden++;
      continue;
    }
    if (count >= MAX_NETWORKS) continue;
    auto &entry = records[count++];
    const auto ssid = result.get_ssid();
    const size_t length = std::min<size_t>(ssid.size(), sizeof(entry.ssid) - 1);
    std::memcpy(entry.ssid, ssid.c_str(), length);
    entry.ssid[length] = 0;
    entry.rssi = result.get_rssi();
    entry.channel = result.get_channel();
    entry.secure = result.get_with_auth();
  }
  visible = this->deduplicate_(records, count);
  this->sort_by_rssi_(records, visible);
}

/** ESP-IDF delivers this listener from the ESPHome main-task event queue. */
void WifiScanApi::on_wifi_scan_results(
    const wifi::wifi_scan_vector_t<wifi::WiFiScanResult> &results) {
  // HTTP never reads this separate, preallocated PSRAM snapshot.
  this->collect_results_(results, this->pending_records_.get(),
                         this->pending_scan_count_, this->pending_hidden_count_);
  this->scan_results_pending_ = true;
}

uint16_t WifiScanApi::deduplicate_(WifiScanEntry *records, uint16_t count) {
  uint16_t unique = 0;
  for (uint16_t index = 0; index < count; index++) {
    auto &candidate = records[index];
    uint16_t match = unique;
    for (uint16_t item = 0; item < unique; item++) {
      if (std::strncmp(records[item].ssid, candidate.ssid, 32) == 0) {
        match = item;
        break;
      }
    }
    if (match == unique) records[unique++] = candidate;
    else if (candidate.rssi > records[match].rssi) records[match] = candidate;
  }
  return unique;
}

void WifiScanApi::sort_by_rssi_(WifiScanEntry *records, uint16_t count) {
  std::sort(records, records + count,
            [](const WifiScanEntry &left, const WifiScanEntry &right) {
              return left.rssi > right.rssi;
            });
}

bool WifiScanApi::append_(size_t *offset, const char *text) {
  const size_t length = std::strlen(text);
  if (*offset + length >= RESPONSE_BYTES) return false;
  std::memcpy(this->response_.get() + *offset, text, length);
  *offset += length;
  return true;
}

bool WifiScanApi::append_ssid_(size_t *offset, const char *ssid) {
  const size_t length = strnlen(ssid, 32);
  for (size_t index = 0; index < length; index++) {
    const uint8_t ch = static_cast<uint8_t>(ssid[index]);
    if (ch == '"' || ch == '\\') {
      char escaped[3]{'\\', static_cast<char>(ch), 0};
      if (!this->append_(offset, escaped)) return false;
    } else if (ch < 0x20) {
      char escaped[7];
      std::snprintf(escaped, sizeof(escaped), "\\u%04x", ch);
      if (!this->append_(offset, escaped)) return false;
    } else {
      char plain[2]{static_cast<char>(ch), 0};
      if (!this->append_(offset, plain)) return false;
    }
  }
  return true;
}

bool WifiScanApi::build_json_(uint16_t visible, uint16_t hidden, size_t *length) {
  size_t offset = 0;
  if (!this->append_(&offset, "{\"ok\":true,\"state\":\"ready\",\"networks\":[")) return false;
  for (uint16_t index = 0; index < visible; index++) {
    const auto &record = this->records_[index];
    if (index && !this->append_(&offset, ",")) return false;
    if (!this->append_(&offset, "{\"ssid\":\"")) return false;
    if (!this->append_ssid_(&offset, record.ssid)) return false;
    char tail[128];
    std::snprintf(tail, sizeof(tail),
                  "\",\"rssi\":%d,\"channel\":%u,\"secure\":%s}",
                  static_cast<int>(record.rssi), record.channel,
                  record.secure ? "true" : "false");
    if (!this->append_(&offset, tail)) return false;
  }
  char tail[64];
  std::snprintf(tail, sizeof(tail), "],\"hidden\":%u}", hidden);
  if (!this->append_(&offset, tail)) return false;
  this->response_[offset] = 0;
  *length = offset;
  return true;
}

void WifiScanApi::send_state_(web_server_idf::AsyncWebServerRequest *request,
                              int code, const char *state) const {
  char body[64];
  std::snprintf(body, sizeof(body), "{\"ok\":true,\"state\":\"%s\"}", state);
  request->send(code, "application/json", body);
}

void WifiScanApi::send_error_(web_server_idf::AsyncWebServerRequest *request,
                              int code, const char *error) const {
  char body[96];
  std::snprintf(body, sizeof(body), "{\"ok\":false,\"state\":\"error\",\"error\":\"%s\"}", error);
  request->send(code, "application/json", body);
}

void WifiScanApi::handleRequest(web_server_idf::AsyncWebServerRequest *request) {
  this->expire_scan_();
  if (request->method() == HTTP_POST) {
    if (!this->start_scan_()) {
      this->send_error_(request, 503, "scan_unavailable");
      return;
    }
    this->send_state_(request, 202, "scanning");
    return;
  }
  if (request->method() != HTTP_GET) {
    this->send_error_(request, 405, "method_not_allowed");
    return;
  }
  if (!this->lock_()) {
    this->send_error_(request, 503, "scan_busy");
    return;
  }
  const auto state = this->state_;
  if (state == WifiScanState::START_PENDING || state == WifiScanState::SCANNING) {
    this->unlock_();
    this->send_state_(request, 202, "scanning");
    return;
  }
  if (state == WifiScanState::IDLE) {
    this->unlock_();
    this->send_state_(request, 200, "idle");
    return;
  }
  if (state == WifiScanState::ERROR) {
    this->unlock_();
    this->send_error_(request, 503, "scan_unavailable");
    return;
  }
  size_t length = 0;
  const bool built = this->build_json_(this->scan_count_, this->hidden_count_, &length);
  if (!built) {
    this->unlock_();
    this->send_error_(request, 500, "response_overflow");
    return;
  }
  // Keep the shared PSRAM response buffer stable until ESP-IDF consumes it.
  httpd_resp_set_hdr(*request, "Cache-Control", "no-store");
  httpd_resp_set_type(*request, "application/json");
  httpd_resp_send(*request, this->response_.get(), length);
  this->unlock_();
}

/** Own one main-task request and allow Wi-Fi a tick before starting. */
bool WifiScanApi::update_scan_lease_() {
  const bool active = this->state_ == WifiScanState::START_PENDING ||
                      this->state_ == WifiScanState::SCANNING;
  if (!active) {
    this->release_high_performance_();
    this->lease_prepared_ = false;
    return false;
  }
  if (this->lease_prepared_ && this->prepared_generation_ == this->scan_generation_)
    return false;
  this->request_high_performance_();
  this->prepared_generation_ = this->scan_generation_;
  this->lease_prepared_ = true;
  return true;
}
/** Ignore an old driver failure when HTTP has queued a new scan. */
void WifiScanApi::consume_radio_failure_() {
  if (!this->radio_start_failed_) return;
  this->radio_start_failed_ = false;
  if (this->state_ == WifiScanState::SCANNING &&
      this->scan_generation_ == this->radio_generation_)
    this->state_ = WifiScanState::ERROR;
}

/** Check the existing deadline while the caller owns the state lock. */
void WifiScanApi::expire_scan_locked_() {
  const bool active = this->state_ == WifiScanState::START_PENDING ||
                      this->state_ == WifiScanState::SCANNING;
  const uint32_t started = this->state_ == WifiScanState::START_PENDING
                               ? this->scan_requested_ms_
                               : this->scan_started_ms_;
  const bool timed_out = active &&
      static_cast<uint32_t>(millis() - started) > SCAN_TIMEOUT_MS;
  if (timed_out) {
    this->state_ = WifiScanState::ERROR;
    ESP_LOGW(TAG, "Wi-Fi scan timed out");
  }
}

}  // namespace esphome::usp_wifi_scan
