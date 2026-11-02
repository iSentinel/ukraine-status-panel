#include "svitlobot_client.h"

#include "esphome/components/network/util.h"
#include "esphome/components/usp_weather/network_gate.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include <algorithm>
#include <cstdio>

namespace esphome::usp_svitlobot {

static const char *const TAG = "usp.svitlobot";
static constexpr char BASE_URL[] =
    "https://api.svitlobot.in.ua/channelPing?channel_key=";
static constexpr char USER_AGENT[] =
    "UkraineStatusPanel/" ESPHOME_PROJECT_VERSION;

namespace {

bool time_reached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

esp_err_t capture_retry_after(esp_http_client_event_t *event) {
  auto *capture =
      static_cast<usp_http_retry_after::HeaderCapture *>(event->user_data);
  if (capture != nullptr && event->event_id == HTTP_EVENT_ON_HEADER) {
    capture->observe(event->header_key, event->header_value);
  }
  return ESP_OK;
}

}  // namespace

void SvitloBotClient::setup() {
  this->next_refresh_ms_ = millis() + this->initial_delay_ms_;
  const BaseType_t result = xTaskCreatePinnedToCoreWithCaps(
      worker_entry, "usp_svitlo", 10240, this, 1, &this->worker_task_, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (result != pdPASS) {
    this->worker_task_ = nullptr;
    this->mark_failed();
    ESP_LOGE(TAG, "Worker could not start");
    return;
  }
  ESP_LOGI(TAG, "Worker started");
}

void SvitloBotClient::loop() {
  if (this->update_pending_.exchange(false)) {
    this->update_trigger_.trigger();
  }
}

void SvitloBotClient::dump_config() {
  ESP_LOGCONFIG(TAG, "SvitloBot Client");
  ESP_LOGCONFIG(TAG, "  Enabled: %s", YESNO(this->enabled()));
  ESP_LOGCONFIG(TAG, "  Configured: %s", YESNO(this->configured()));
  ESP_LOGCONFIG(TAG, "  Refresh: %lu ms",
                static_cast<unsigned long>(this->refresh_interval_ms_));
  ESP_LOGCONFIG(TAG, "  Worker: %s", YESNO(this->worker_task_ != nullptr));
}

void SvitloBotClient::set_enabled(bool value) {
  const bool previous = this->enabled_.exchange(value);
  if (value && !previous) this->ping_requested_.store(true);
  this->update_pending_.store(true);
}

void SvitloBotClient::set_key(const std::string &value) {
  {
    std::lock_guard<std::mutex> lock(this->key_mutex_);
    this->key_ = value;
  }
  if (this->enabled() && value.size() >= 4U) {
    this->ping_requested_.store(true);
  }
  this->update_pending_.store(true);
}

void SvitloBotClient::request_ping() {
  if (this->enabled() && this->configured()) {
    this->ping_requested_.store(true);
  }
}

bool SvitloBotClient::configured() const { return this->key_copy_().size() >= 4U; }

bool SvitloBotClient::last_ok() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.ok;
}

int SvitloBotClient::last_code() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.code;
}

uint32_t SvitloBotClient::last_duration_ms() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.duration_ms;
}

uint32_t SvitloBotClient::error_count() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.errors;
}

uint32_t SvitloBotClient::sequence() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.sequence;
}

void SvitloBotClient::worker_entry(void *argument) {
  auto *client = static_cast<SvitloBotClient *>(argument);
  client->worker_loop();
  client->worker_task_ = nullptr;
  vTaskDelete(nullptr);
}

void SvitloBotClient::worker_loop() {
  while (!this->stop_worker_.load(std::memory_order_acquire)) {
    const uint32_t now = millis();
    const bool manual_pending =
        this->ping_requested_.load(std::memory_order_acquire);
    const bool due = time_reached(now, this->next_refresh_ms_);
    if ((manual_pending || due) && this->enabled() && this->configured() &&
        this->retry_after_gate_.ready(now)) {
      bool manual = false;
      if (manual_pending) {
        manual = this->ping_requested_.exchange(
            false, std::memory_order_acq_rel);
      }
      this->perform_ping_(!manual);
      this->next_refresh_ms_ = millis() + this->refresh_interval_ms_;
    } else if (due && (!this->enabled() || !this->configured())) {
      this->next_refresh_ms_ = now + this->refresh_interval_ms_;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void SvitloBotClient::perform_ping_(bool scheduled) {
  if (!network::is_connected()) {
    this->commit_result_(false, 0, 0);
    return;
  }
  const std::string key = this->key_copy_();
  if (key.size() < 4U) return;
  const std::string url = std::string(BASE_URL) + key;
  usp_network::HttpsTransaction network_tx(
      "svitlobot", usp_network::HttpsPriority::BACKGROUND, 500);
  if (!network_tx.acquired()) {
    ESP_LOGW(TAG, "SvitloBot ping skipped: HTTPS gate busy");
    this->commit_result_(false, 0, 0);
    return;
  }
  usp_http_retry_after::HeaderCapture retry_after;
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = 8000;
  config.user_agent = USER_AGENT;
  config.event_handler = capture_retry_after;
  config.user_data = &retry_after;
  config.buffer_size = 512;
  config.buffer_size_tx = 512;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  config.crt_bundle_attach = esp_crt_bundle_attach;
#endif
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    this->commit_result_(false, 0, 0);
    return;
  }
  const uint32_t started = millis();
  const esp_err_t error = esp_http_client_perform(client);
  const uint32_t duration = millis() - started;
  const int status = esp_http_client_get_status_code(client);
  if (status == 429) {
    const uint32_t delay = this->retry_after_gate_.apply(
        status, retry_after, this->refresh_interval_ms_, millis());
    ESP_LOGW(TAG, "SvitloBot rate limited retry_after_ms=%lu",
             static_cast<unsigned long>(delay));
  }
  esp_http_client_cleanup(client);
  const bool ok = error == ESP_OK && status == 200;
  this->commit_result_(ok, status, duration);
  ESP_LOGI(TAG, "Ping %s status=%d duration=%lu ms scheduled=%s",
           ok ? "OK" : "failed", status,
           static_cast<unsigned long>(duration), YESNO(scheduled));
}

std::string SvitloBotClient::key_copy_() const {
  std::lock_guard<std::mutex> lock(this->key_mutex_);
  return this->key_;
}

void SvitloBotClient::commit_result_(bool ok, int code, uint32_t duration_ms) {
  {
    std::lock_guard<std::mutex> lock(this->state_mutex_);
    this->snapshot_.valid = true;
    this->snapshot_.ok = ok;
    this->snapshot_.code = code;
    this->snapshot_.duration_ms = duration_ms;
    this->snapshot_.sequence++;
    if (ok) this->snapshot_.errors = 0;
    else this->snapshot_.errors++;
  }
  this->update_pending_.store(true, std::memory_order_release);
}

}  // namespace esphome::usp_svitlobot
