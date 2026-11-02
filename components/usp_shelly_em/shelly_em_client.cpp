#include "shelly_em_client.h"

#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace esphome::usp_shelly_em {

static const char *const TAG = "usp.shelly_em";
static constexpr size_t MAX_BODY_BYTES = 2048;
static constexpr char USER_AGENT[] =
    "UkraineStatusPanel/" ESPHOME_PROJECT_VERSION;

namespace {

struct BodyCapture {
  std::string body;
  bool overflow{false};
};

bool time_reached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

esp_err_t capture_body(esp_http_client_event_t *event) {
  auto *capture = static_cast<BodyCapture *>(event->user_data);
  if (capture == nullptr || event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
  if (event->data_len <= 0) return ESP_OK;
  const size_t length = static_cast<size_t>(event->data_len);
  if (capture->body.size() + length > MAX_BODY_BYTES) {
    capture->overflow = true;
    return ESP_FAIL;
  }
  capture->body.append(static_cast<const char *>(event->data), length);
  return ESP_OK;
}

bool read_number(JsonObjectConst root, const char *key, float *value,
                 bool required) {
  JsonVariantConst item = root[key];
  if (item.isNull()) {
    *value = NAN;
    return !required;
  }
  if (!item.is<float>()) return false;
  *value = item.as<float>();
  return std::isfinite(*value);
}

bool parse_channel(const std::string &body, uint8_t expected,
                   Em1Channel *channel) {
  auto document = json::parse_json(body);
  if (document.isNull() || !document.is<JsonObject>()) return false;
  JsonObjectConst root = document.as<JsonObjectConst>();
  if (!root["id"].is<int>() || root["id"].as<int>() != expected) return false;
  Em1Channel next{};
  if (!read_number(root, "voltage", &next.voltage, true) ||
      !read_number(root, "current", &next.current, true) ||
      !read_number(root, "act_power", &next.act_power, true)) return false;
  if (!read_number(root, "aprt_power", &next.aprt_power, false) ||
      !read_number(root, "pf", &next.pf, false) ||
      !read_number(root, "freq", &next.freq, false)) return false;
  JsonArrayConst errors = root["errors"].as<JsonArrayConst>();
  next.meter_error = !errors.isNull() && errors.size() != 0;
  next.valid = true;
  *channel = next;
  return true;
}

}  // namespace

void ShellyEmClient::setup() {
  this->next_refresh_ms_ = millis() + this->initial_delay_ms_;
  const BaseType_t result = xTaskCreatePinnedToCoreWithCaps(
      worker_entry, "usp_shelly", 8192, this, 1, &this->worker_task_, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (result == pdPASS) {
    ESP_LOGI(TAG, "Worker started");
    return;
  }
  this->worker_task_ = nullptr;
  this->mark_failed();
  ESP_LOGE(TAG, "Worker could not start");
}

void ShellyEmClient::loop() {
  if (this->update_pending_.exchange(false)) this->update_trigger_.trigger();
}

void ShellyEmClient::dump_config() {
  ESP_LOGCONFIG(TAG, "Shelly Pro EM-50 Monitor");
  ESP_LOGCONFIG(TAG, "  Enabled: %s", YESNO(this->enabled()));
  ESP_LOGCONFIG(TAG, "  Endpoint: %s:%u", this->host().c_str(),
                static_cast<unsigned>(this->port()));
  ESP_LOGCONFIG(TAG, "  Refresh: %lu ms",
                static_cast<unsigned long>(this->refresh_interval_ms_));
  ESP_LOGCONFIG(TAG, "  Timeout: %lu ms",
                static_cast<unsigned long>(this->connect_timeout_ms_));
}

void ShellyEmClient::set_enabled(bool value) {
  const bool previous = this->enabled_.exchange(value);
  if (value && !previous) this->refresh_requested_.store(true);
  this->update_pending_.store(true);
}

void ShellyEmClient::set_host(const std::string &value) {
  {
    std::lock_guard<std::mutex> lock(this->config_mutex_);
    this->host_ = value;
  }
  if (this->enabled() && !value.empty()) this->refresh_requested_.store(true);
  this->update_pending_.store(true);
}

void ShellyEmClient::set_port(uint16_t value) {
  this->port_.store(value == 0 ? 80 : value, std::memory_order_release);
  if (this->enabled()) this->refresh_requested_.store(true);
  this->update_pending_.store(true);
}

void ShellyEmClient::set_password(const std::string &value) {
  {
    std::lock_guard<std::mutex> lock(this->config_mutex_);
    this->password_ = value;
  }
  if (this->enabled() && this->configured()) this->refresh_requested_.store(true);
}

void ShellyEmClient::request_refresh() {
  if (this->enabled() && this->configured()) this->refresh_requested_.store(true);
}

bool ShellyEmClient::configured() const {
  return !this->host_copy_().empty() && this->port() != 0;
}

bool ShellyEmClient::reachable() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.reachable;
}

bool ShellyEmClient::complete() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.complete;
}

bool ShellyEmClient::auth_required() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.auth_required;
}

Em1Channel ShellyEmClient::channel(uint8_t index) const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return index < this->snapshot_.channels.size()
             ? this->snapshot_.channels[index] : Em1Channel{};
}

int ShellyEmClient::last_code() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.last_code;
}

uint32_t ShellyEmClient::last_duration_ms() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.duration_ms;
}

uint32_t ShellyEmClient::error_count() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.errors;
}

uint32_t ShellyEmClient::sequence() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.sequence;
}

std::string ShellyEmClient::host() const { return this->host_copy_(); }

void ShellyEmClient::worker_entry(void *argument) {
  auto *client = static_cast<ShellyEmClient *>(argument);
  client->worker_loop();
  client->worker_task_ = nullptr;
  vTaskDelete(nullptr);
}

void ShellyEmClient::worker_loop() {
  while (!this->stop_worker_.load(std::memory_order_acquire)) {
    const uint32_t now = millis();
    const bool manual = this->refresh_requested_.exchange(false);
    const bool due = time_reached(now, this->next_refresh_ms_);
    if ((manual || due) && this->enabled() && this->configured()) {
      this->perform_refresh_(!manual);
      this->next_refresh_ms_ = millis() + this->refresh_interval_ms_;
    } else if (due) {
      this->next_refresh_ms_ = now + this->refresh_interval_ms_;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void ShellyEmClient::perform_refresh_(bool scheduled) {
  Snapshot next{};
  if (!network::is_connected()) {
    next.errors = this->error_count() + 1;
    next.sequence = this->sequence() + 1;
    this->commit_(next);
    return;
  }
  const uint32_t started = millis();
  bool auth = false;
  int code = 0;
  uint32_t request_ms = 0;
  const bool first = this->fetch_channel_(0, &next.channels[0], &code,
                                          &request_ms, &auth);
  next.last_code = code;
  next.auth_required = auth;
  const bool second = (!auth && (first || code > 0))
      ? this->fetch_channel_(1, &next.channels[1], &code, &request_ms, &auth)
      : false;
  next.last_code = code;
  next.auth_required = next.auth_required || auth;
  next.reachable = first || second || next.last_code > 0;
  next.complete = first && second;
  next.duration_ms = millis() - started;
  next.errors = next.complete ? 0 : this->error_count() + 1;
  next.sequence = this->sequence() + 1;
  this->commit_(next);
  ESP_LOGI(TAG, "Refresh complete=%s code=%d duration=%lu ms scheduled=%s",
           YESNO(next.complete), next.last_code,
           static_cast<unsigned long>(next.duration_ms), YESNO(scheduled));
}

bool ShellyEmClient::fetch_channel_(uint8_t index, Em1Channel *channel,
                                    int *code, uint32_t *duration_ms,
                                    bool *auth_required) {
  const std::string host = this->host_copy_();
  const std::string password = this->password_copy_();
  const std::string url = "http://" + host + ":" +
      std::to_string(this->port()) + "/rpc/EM1.GetStatus?id=" +
      std::to_string(index);
  BodyCapture capture;
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = static_cast<int>(this->connect_timeout_ms_);
  config.user_agent = USER_AGENT;
  config.event_handler = capture_body;
  config.user_data = &capture;
  config.buffer_size = 1024;
  config.buffer_size_tx = 256;
  if (!password.empty()) {
    config.username = "admin";
    config.password = password.c_str();
    config.auth_type = HTTP_AUTH_TYPE_DIGEST;
  }
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) return false;
  const uint32_t started = millis();
  const esp_err_t error = esp_http_client_perform(client);
  *duration_ms = millis() - started;
  *code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  *auth_required = *code == 401;
  if (error != ESP_OK || *code != 200 || capture.overflow) return false;
  return parse_channel(capture.body, index, channel);
}

std::string ShellyEmClient::host_copy_() const {
  std::lock_guard<std::mutex> lock(this->config_mutex_);
  return this->host_;
}

std::string ShellyEmClient::password_copy_() const {
  std::lock_guard<std::mutex> lock(this->config_mutex_);
  return this->password_;
}

void ShellyEmClient::commit_(const Snapshot &next) {
  {
    std::lock_guard<std::mutex> lock(this->state_mutex_);
    this->snapshot_ = next;
  }
  this->update_pending_.store(true, std::memory_order_release);
}

}  // namespace esphome::usp_shelly_em
