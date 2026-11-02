#include "neptun_client.h"

#include "esphome/components/network/util.h"
#include "esphome/components/usp_weather/network_gate.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace esphome::usp_neptun {

static const char *const TAG = "usp.neptun";
static constexpr char THREATS_URL[] = "https://neptun.in.ua/api/v1/threats";
static constexpr char ALERTS_URL[] = "https://neptun.in.ua/api/v1/alerts";
static constexpr char USER_AGENT[] =
    "UkraineStatusPanel/" ESPHOME_PROJECT_VERSION;

namespace {

bool time_reached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

struct HttpScanContext {
  void *scanner{nullptr};
  void (*feed)(void *, const char *, size_t){nullptr};
  bool (*is_overflow)(const void *){nullptr};
  uint8_t connected_events{0};
  usp_http_retry_after::HeaderCapture retry_after{};
};

template<typename Scanner>
void scanner_feed(void *value, const char *data, size_t len) {
  static_cast<Scanner *>(value)->feed(data, len);
}

template<typename Scanner>
bool scanner_overflow(const void *value) {
  return static_cast<const Scanner *>(value)->is_overflow();
}

template<typename Scanner>
HttpScanContext make_scan_context(Scanner *scanner) {
  return {scanner, scanner_feed<Scanner>, scanner_overflow<Scanner>, 0, {}};
}

esp_err_t scan_http_body(esp_http_client_event_t *event) {
  auto *context = static_cast<HttpScanContext *>(event->user_data);
  if (context == nullptr) return ESP_OK;
  if (event->event_id == HTTP_EVENT_ON_CONNECTED) {
    ++context->connected_events;
    return ESP_OK;
  }
  if (event->event_id == HTTP_EVENT_ON_HEADER) {
    context->retry_after.observe(event->header_key, event->header_value);
    return ESP_OK;
  }
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
    return ESP_OK;
  }
  context->feed(
      context->scanner, static_cast<const char *>(event->data), event->data_len);
  return context->is_overflow(context->scanner) ? ESP_FAIL : ESP_OK;
}

struct HttpScanResult {
  esp_err_t error{ESP_FAIL};
  int status{0};
  uint8_t connected_events{0};
  uint32_t request_ms{0};
  usp_http_retry_after::HeaderCapture retry_after{};
};

template<typename Scanner>
HttpScanResult perform_scan(
    esp_http_client_handle_t client, const char *url, Scanner *scanner) {
  HttpScanResult result;
  if (esp_http_client_set_url(client, url) != ESP_OK) return result;
  auto context = make_scan_context(scanner);
  esp_http_client_set_user_data(client, &context);
  const uint32_t started_ms = millis();
  result.error = esp_http_client_perform(client);
  result.request_ms = millis() - started_ms;
  result.status = esp_http_client_get_status_code(client);
  result.connected_events = context.connected_events;
  result.retry_after = context.retry_after;
  esp_http_client_set_user_data(client, nullptr);
  esp_http_client_close(client);
  return result;
}

}  // namespace

void NeptunClient::setup() {
  this->next_refresh_ms_ = millis() + this->initial_delay_ms_;
  // Keep the new NEPTUN worker from consuming scarce internal DRAM.
  // ESP-IDF 5.5.5 allows task stacks in PSRAM on ESP32-S3; the TCB remains
  // internal. This preserves internal heap for RSA certificate verification.
  const BaseType_t result = xTaskCreatePinnedToCoreWithCaps(
      worker_entry, "usp_neptun", 8192, this, 1, &this->worker_task_, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (result != pdPASS) {
    this->worker_task_ = nullptr;
    this->mark_failed();
    ESP_LOGE(TAG, "NEPTUN worker could not start");
    return;
  }
  ESP_LOGI(TAG, "NEPTUN worker started");
}

void NeptunClient::loop() {
  if (this->update_pending_.exchange(false)) {
    this->update_trigger_.trigger();
    this->log_snapshot_();
  }
  const bool now_stale = this->stale();
  if (now_stale != this->last_stale_state_) {
    this->last_stale_state_ = now_stale;
    this->update_trigger_.trigger();
    ESP_LOGI(TAG, "NEPTUN stale state=%s", YESNO(now_stale));
  }
}

void NeptunClient::dump_config() {
  ESP_LOGCONFIG(TAG, "NEPTUN Threat Summary");
  ESP_LOGCONFIG(TAG, "  Threat endpoint: %s", THREATS_URL);
  ESP_LOGCONFIG(TAG, "  Alert endpoint: %s", ALERTS_URL);
  ESP_LOGCONFIG(TAG, "  Active refresh: %lu ms",
                static_cast<unsigned long>(this->refresh_interval_ms_));
  ESP_LOGCONFIG(TAG, "  Idle refresh: %lu ms",
                static_cast<unsigned long>(this->idle_refresh_interval_ms_));
  ESP_LOGCONFIG(TAG, "  Stale after: %lu ms",
                static_cast<unsigned long>(this->stale_after_ms_));
  ESP_LOGCONFIG(TAG, "  Worker: %s", YESNO(this->worker_task_ != nullptr));
}

void NeptunClient::request_refresh() {
  this->refresh_requested_.store(true, std::memory_order_release);
}

void NeptunClient::dump_runtime() const {
  this->log_snapshot_();
}

void NeptunClient::worker_entry(void *argument) {
  auto *client = static_cast<NeptunClient *>(argument);
  client->worker_loop();
  client->reset_http_client_();
  vTaskDeleteWithCaps(nullptr);
}

void NeptunClient::worker_loop() {
  while (!this->stop_worker_.load(std::memory_order_acquire)) {
    const uint32_t now = millis();
    const bool manual_pending =
        this->refresh_requested_.load(std::memory_order_acquire);
    const bool due = time_reached(now, this->next_refresh_ms_);
    if ((manual_pending || due) && this->retry_after_gate_.ready(now)) {
      bool manual = false;
      if (manual_pending) {
        manual = this->refresh_requested_.exchange(
            false, std::memory_order_acq_rel);
      }
      this->process_request(!manual);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void NeptunClient::process_request(bool scheduled) {
  this->https_gate_busy_during_request_ = false;
  Snapshot next = this->snapshot_copy_();
  const bool fetched = this->fetch_snapshot_(&next);
  if (fetched) {
    {
      std::lock_guard<std::mutex> lock(this->state_mutex_);
      next.sequence = this->snapshot_.sequence + 1U;
      this->snapshot_ = next;
    }
  } else {
    std::lock_guard<std::mutex> lock(this->state_mutex_);
    this->snapshot_.fetch_ok = false;
    this->snapshot_.alerts_fetch_ok = false;
  }
  if (this->https_gate_busy_during_request_)
    this->schedule_gate_retry_();
  else if (fetched)
    this->schedule_success_();
  else
    this->schedule_retry_();
  this->update_pending_.store(true, std::memory_order_release);
  ESP_LOGI(TAG, "NEPTUN request %s ok=%s",
           scheduled ? "scheduled" : "manual", YESNO(fetched));
}

bool NeptunClient::ensure_http_client_() {
  if (this->http_client_ != nullptr) return true;

  esp_http_client_config_t config = {};
  config.url = THREATS_URL;
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = 7000;
  config.user_agent = USER_AGENT;
  config.event_handler = scan_http_body;
  config.buffer_size = 2048;
  config.buffer_size_tx = 1024;
#if CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
  // Preserve only the compact TLS session ticket between polls. The active
  // TLS transport is closed after every response so its large buffers return
  // to internal heap for Weather, Outage and the Native API.
  config.save_client_session = true;
#endif
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  config.crt_bundle_attach = esp_crt_bundle_attach;
#endif

  this->http_client_ = esp_http_client_init(&config);
  if (this->http_client_ == nullptr) {
    ESP_LOGW(TAG, "NEPTUN HTTP client initialization failed");
    return false;
  }
  esp_http_client_set_header(this->http_client_, "Accept", "application/json");
  esp_http_client_set_header(this->http_client_, "Accept-Encoding", "identity");
  ESP_LOGI(TAG, "NEPTUN HTTPS client initialized with session tickets=%s",
#if CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
           "YES"
#else
           "NO"
#endif
  );
  return true;
}

void NeptunClient::reset_http_client_() {
  if (this->http_client_ == nullptr) return;
  esp_http_client_cleanup(this->http_client_);
  this->http_client_ = nullptr;
}

bool NeptunClient::fetch_snapshot_(Snapshot *snapshot) {
  if (!network::is_connected()) {
    ESP_LOGW(TAG, "NEPTUN request skipped: network is offline");
    return false;
  }
  const bool threats_ok = this->fetch_threats_(snapshot);
  if (!this->retry_after_gate_.ready(millis())) {
    snapshot->fetch_ok = false;
    snapshot->alerts_fetch_ok = false;
    return false;
  }
  const bool alerts_ok = this->fetch_alerts_(snapshot);
  snapshot->fetch_ok = threats_ok;
  snapshot->alerts_fetch_ok = alerts_ok;
  // A failure in target details must not prevent fresh regional alert data.
  return threats_ok || alerts_ok;
}

bool NeptunClient::fetch_threats_(Snapshot *snapshot) {
  usp_network::HttpsTransaction network_tx(
      "neptun.threats", usp_network::HttpsPriority::CRITICAL, 500);
  if (!network_tx.acquired()) {
    this->https_gate_busy_during_request_ = true;
    ESP_LOGW(TAG, "NEPTUN threats skipped: HTTPS gate busy");
    return false;
  }
  ThreatScanner scanner;
  if (!this->ensure_http_client_()) return false;
  const auto result = perform_scan(this->http_client_, THREATS_URL, &scanner);
  this->transport_connections_ += result.connected_events;
  if (result.status == 429) {
    const uint32_t delay = this->retry_after_gate_.apply(
        result.status, result.retry_after, this->retry_delay_ms_, millis());
    ESP_LOGW(TAG, "NEPTUN threats rate limited retry_after_ms=%lu",
             static_cast<unsigned long>(delay));
  }
  if (result.error != ESP_OK || result.status != 200 || scanner.is_overflow() ||
      !scanner.complete()) {
    ESP_LOGW(TAG, "NEPTUN threats HTTP failed err=%s status=%d bytes=%lu overflow=%s request_ms=%lu",
             esp_err_to_name(result.error), result.status,
             static_cast<unsigned long>(scanner.result().bytes),
             YESNO(scanner.result().overflow),
             static_cast<unsigned long>(result.request_ms));
    return false;
  }
  snapshot->valid = true;
  snapshot->fetch_ok = true;
  snapshot->received_ms = millis();
  const auto &threats = scanner.result();
  snapshot->body_bytes = threats.bytes;
  snapshot->threat_entries = threats.entries;
  snapshot->total = threats.global.total;
  snapshot->uav = threats.global.uav;
  snapshot->fpv = threats.global.fpv;
  snapshot->recon = threats.global.recon;
  snapshot->missile = threats.global.missile;
  snapshot->ballistic = threats.global.ballistic;
  snapshot->kab = threats.global.kab;
  snapshot->mig31k = threats.global.mig31k;
  snapshot->unknown = threats.global.unknown;
  snapshot->threat_unknown_regions = threats.unknown_regions;
  snapshot->region_threats = threats.regions;
  snapshot->active_region_threats = threats.active_regions;
  snapshot->threat_server_epoch = threats.server_epoch;
  snapshot->threat_point_candidates = threats.point_candidates;
  snapshot->threat_point_count = threats.point_count;
  snapshot->threat_point_dropped_capacity = threats.point_dropped_capacity;
  snapshot->threat_points = threats.points;
  ESP_LOGD(TAG, "NEPTUN threats new_conn=%u conn_total=%lu request_ms=%lu",
           result.connected_events,
           static_cast<unsigned long>(this->transport_connections_),
           static_cast<unsigned long>(result.request_ms));
  return true;
}

bool NeptunClient::fetch_alerts_(Snapshot *snapshot) {
  usp_network::HttpsTransaction network_tx(
      "neptun.alerts", usp_network::HttpsPriority::CRITICAL, 500);
  if (!network_tx.acquired()) {
    this->https_gate_busy_during_request_ = true;
    ESP_LOGW(TAG, "NEPTUN alerts skipped: HTTPS gate busy");
    return false;
  }
  AlertScanner scanner;
  if (!this->ensure_http_client_()) return false;
  const auto result = perform_scan(this->http_client_, ALERTS_URL, &scanner);
  this->transport_connections_ += result.connected_events;
  if (result.status == 429) {
    const uint32_t delay = this->retry_after_gate_.apply(
        result.status, result.retry_after, this->retry_delay_ms_, millis());
    ESP_LOGW(TAG, "NEPTUN alerts rate limited retry_after_ms=%lu",
             static_cast<unsigned long>(delay));
  }
  const auto &alerts = scanner.result();
  if (result.error != ESP_OK || result.status != 200 || alerts.overflow ||
      !scanner.complete()) {
    ESP_LOGW(TAG, "NEPTUN alerts HTTP failed err=%s status=%d bytes=%lu overflow=%s request_ms=%lu",
             esp_err_to_name(result.error), result.status,
             static_cast<unsigned long>(alerts.bytes), YESNO(alerts.overflow),
             static_cast<unsigned long>(result.request_ms));
    return false;
  }
  snapshot->alerts_valid = true;
  snapshot->alerts_fetch_ok = true;
  snapshot->alerts_received_ms = millis();
  snapshot->alerts_body_bytes = alerts.bytes;
  snapshot->alert_entries = alerts.entries;
  snapshot->alert_unknown_regions = alerts.unknown_regions;
  snapshot->alert_unknown_levels = alerts.unknown_levels;
  snapshot->alert_levels = alerts.levels;
  ESP_LOGD(TAG, "NEPTUN alerts new_conn=%u conn_total=%lu request_ms=%lu entries=%u",
           result.connected_events,
           static_cast<unsigned long>(this->transport_connections_),
           static_cast<unsigned long>(result.request_ms), alerts.entries);
  return true;
}

void NeptunClient::schedule_success_() {
  this->retry_delay_ms_ = MIN_RETRY_MS;
  const Snapshot snap = this->snapshot_copy_();
  const uint32_t interval = snap.threat_entries > 0U
                                ? this->refresh_interval_ms_
                                : this->idle_refresh_interval_ms_;
  this->next_refresh_ms_ = millis() + interval;
  ESP_LOGD(TAG, "NEPTUN next refresh in %lu ms active=%s",
           static_cast<unsigned long>(interval), YESNO(snap.threat_entries > 0U));
}

void NeptunClient::schedule_retry_() {
  this->next_refresh_ms_ = millis() + this->retry_delay_ms_;
  this->retry_delay_ms_ =
      std::min<uint32_t>(this->retry_delay_ms_ * 2U, MAX_RETRY_MS);
}

void NeptunClient::schedule_gate_retry_() {
  this->next_refresh_ms_ = millis() + GATE_BUSY_RETRY_MS;
  ESP_LOGD(TAG, "NEPTUN HTTPS gate busy; retry in %lu ms without backoff growth",
           static_cast<unsigned long>(GATE_BUSY_RETRY_MS));
}

NeptunClient::Snapshot NeptunClient::snapshot_copy_() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_;
}

bool NeptunClient::valid() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.valid;
}

bool NeptunClient::stale() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return !this->snapshot_.valid ||
         millis() - this->snapshot_.received_ms > this->stale_after_ms_;
}

bool NeptunClient::last_fetch_ok() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.fetch_ok;
}

uint16_t NeptunClient::total() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.total;
}

uint16_t NeptunClient::uav() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.uav;
}

uint16_t NeptunClient::fpv() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.fpv;
}

uint16_t NeptunClient::recon() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.recon;
}

uint16_t NeptunClient::missile() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.missile;
}

uint16_t NeptunClient::ballistic() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.ballistic;
}

uint16_t NeptunClient::kab() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.kab;
}

uint16_t NeptunClient::mig31k() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.mig31k;
}

uint16_t NeptunClient::unknown() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.unknown;
}

ThreatCounts NeptunClient::threat_counts() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return {this->snapshot_.total, this->snapshot_.uav, this->snapshot_.fpv,
          this->snapshot_.recon, this->snapshot_.missile,
          this->snapshot_.ballistic, this->snapshot_.kab,
          this->snapshot_.mig31k, this->snapshot_.unknown};
}

uint32_t NeptunClient::sequence() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.sequence;
}

uint32_t NeptunClient::body_bytes() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.body_bytes;
}

ThreatCounts NeptunClient::region_threat_counts(uint16_t region_id) const {
  const int index = region_alert_index(region_id);
  if (index < 0) return {};
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->snapshot_.valid) return {};
  return this->snapshot_.region_threats[static_cast<size_t>(index)];
}

uint16_t NeptunClient::threat_unknown_region_count() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.threat_unknown_regions;
}

void NeptunClient::dump_region_threats(uint16_t region_id) const {
  const auto c = this->region_threat_counts(region_id);
  ESP_LOGI(TAG,
           "NEPTUN_THREATS_REGION region=%u valid=%s stale=%s fetch_ok=%s "
           "total=%u uav=%u fpv=%u recon=%u missile=%u ballistic=%u "
           "kab=%u mig31k=%u unknown=%u",
           region_id, YESNO(this->valid()), YESNO(this->stale()),
           YESNO(this->last_fetch_ok()), c.total, c.uav, c.fpv, c.recon,
           c.missile, c.ballistic, c.kab, c.mig31k, c.unknown);
}

bool NeptunClient::alerts_valid() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.alerts_valid;
}

bool NeptunClient::alerts_stale() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return !this->snapshot_.alerts_valid ||
         millis() - this->snapshot_.alerts_received_ms > this->stale_after_ms_;
}

bool NeptunClient::alerts_last_fetch_ok() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.alerts_fetch_ok;
}

uint8_t NeptunClient::region_alert_level(uint16_t region_id) const {
  const int index = region_alert_index(region_id);
  if (index < 0) return 0;
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->snapshot_.alerts_valid) return 0;
  return this->snapshot_.alert_levels[static_cast<size_t>(index)];
}

uint16_t NeptunClient::alert_entry_count() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.alert_entries;
}

uint16_t NeptunClient::alert_unknown_region_count() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.alert_unknown_regions;
}

uint16_t NeptunClient::alert_unknown_level_count() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.alert_unknown_levels;
}

uint32_t NeptunClient::alerts_body_bytes() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.alerts_body_bytes;
}

void NeptunClient::dump_region_alert(uint16_t region_id) const {
  ESP_LOGI(TAG, "NEPTUN_REGION region=%u valid=%s stale=%s fetch_ok=%s level=%u",
           region_id, YESNO(this->alerts_valid()), YESNO(this->alerts_stale()),
           YESNO(this->alerts_last_fetch_ok()), this->region_alert_level(region_id));
}

void NeptunClient::log_snapshot_() const {
  const auto s = this->snapshot_copy_();
  ESP_LOGI(TAG,
           "NEPTUN valid=%s stale=%s fetch_ok=%s seq=%lu bytes=%lu total=%u "
           "uav=%u fpv=%u recon=%u missile=%u ballistic=%u kab=%u mig31k=%u unknown=%u unknown_regions=%u",
           YESNO(s.valid), YESNO(this->stale()), YESNO(s.fetch_ok),
           static_cast<unsigned long>(s.sequence),
           static_cast<unsigned long>(s.body_bytes), s.total, s.uav, s.fpv,
           s.recon, s.missile, s.ballistic, s.kab, s.mig31k, s.unknown,
           s.threat_unknown_regions);
  ESP_LOGI(TAG,
           "NEPTUN_POINTS count=%u candidates=%u dropped_capacity=%u ttl_s=%lu",
           s.threat_point_count, s.threat_point_candidates,
           s.threat_point_dropped_capacity,
           static_cast<unsigned long>(THREAT_POINT_TTL_SECONDS));
  ESP_LOGI(TAG,
           "NEPTUN_ALERTS valid=%s stale=%s fetch_ok=%s bytes=%lu entries=%u "
           "unknown_regions=%u unknown_levels=%u kyiv_oblast=%u kyiv_city=%u",
           YESNO(s.alerts_valid), YESNO(this->alerts_stale()),
           YESNO(s.alerts_fetch_ok),
           static_cast<unsigned long>(s.alerts_body_bytes), s.alert_entries,
           s.alert_unknown_regions, s.alert_unknown_levels,
           this->region_alert_level(14), this->region_alert_level(31));
}

}  // namespace esphome::usp_neptun
