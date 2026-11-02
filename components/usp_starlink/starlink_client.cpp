#include "starlink_client.h"

#include "starlink_grpc.h"

#include "esphome/components/network/util.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#include "esp_heap_caps.h"

namespace esphome::usp_starlink {

static const char *const TAG = "usp.starlink";

namespace {

bool time_reached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

const char *outcome_name(RpcOutcome outcome) {
  switch (outcome) {
    case RpcOutcome::OK:
      return "OK";
    case RpcOutcome::REMOTE_ERROR:
      return "REMOTE_ERROR";
    case RpcOutcome::RESULT_UNKNOWN:
      return "RESULT_UNKNOWN";
    case RpcOutcome::PROTOCOL_ERROR:
      return "PROTOCOL_ERROR";
    default:
      return "NOT_SENT";
  }
}

}  // namespace

void StarlinkClient::setup() {
  this->next_refresh_ms_ = millis() + this->initial_delay_ms_;
  const BaseType_t result = xTaskCreatePinnedToCoreWithCaps(
      worker_entry, "usp_starlink", 8192, this, 1, &this->worker_task_, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (result != pdPASS) {
    this->worker_task_ = nullptr;
    this->mark_failed();
    ESP_LOGE(TAG, "Worker could not start");
    return;
  }
  ESP_LOGI(TAG, "Worker started");
}

void StarlinkClient::loop() {
  if (this->update_pending_.exchange(false)) this->update_trigger_.trigger();
}

void StarlinkClient::dump_config() {
  ESP_LOGCONFIG(TAG, "Starlink Dishy gRPC Monitor");
  ESP_LOGCONFIG(TAG, "  Enabled: %s", YESNO(this->enabled()));
  ESP_LOGCONFIG(TAG, "  Endpoint: %s:%u", this->host().c_str(),
                static_cast<unsigned>(this->port()));
  ESP_LOGCONFIG(TAG, "  Refresh: %lu ms",
                static_cast<unsigned long>(this->refresh_interval_ms_));
  ESP_LOGCONFIG(TAG, "  Timeout: %lu ms",
                static_cast<unsigned long>(this->connect_timeout_ms_));
}

void StarlinkClient::set_enabled(bool value) {
  const bool previous = this->enabled_.exchange(value);
  if (value && !previous) this->probe_requested_.store(true);
  this->update_pending_.store(true);
}

void StarlinkClient::set_host(const std::string &value) {
  {
    std::lock_guard<std::mutex> lock(this->host_mutex_);
    this->host_ = value.empty() ? "192.168.100.1" : value;
  }
  if (this->enabled()) this->probe_requested_.store(true);
  this->update_pending_.store(true);
}

void StarlinkClient::set_port(uint16_t value) {
  this->port_.store(value == 0 ? 9200 : value, std::memory_order_release);
  if (this->enabled()) this->probe_requested_.store(true);
  this->update_pending_.store(true);
}

void StarlinkClient::request_probe() {
  if (this->enabled() && this->configured()) this->probe_requested_.store(true);
}

void StarlinkClient::request_gps_inhibit(bool inhibit) {
  if (!this->enabled() || !this->configured()) return;
  this->gps_inhibit_requested_.store(inhibit ? 1 : 0,
                                     std::memory_order_release);
}

bool StarlinkClient::configured() const {
  return !this->host_copy_().empty() && this->port() != 0;
}

bool StarlinkClient::reachable() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.reachable;
}

bool StarlinkClient::gps_status_valid() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.gps_status_valid;
}

bool StarlinkClient::gps_valid() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.gps_valid;
}

bool StarlinkClient::inhibit_gps() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.inhibit_gps;
}

uint32_t StarlinkClient::gps_satellites() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.gps_satellites;
}

int StarlinkClient::grpc_status() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.grpc_status;
}

uint32_t StarlinkClient::latency_ms() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.latency_ms;
}

uint32_t StarlinkClient::error_count() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.errors;
}

uint32_t StarlinkClient::sequence() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.sequence;
}

std::string StarlinkClient::host() const { return this->host_copy_(); }

void StarlinkClient::worker_entry(void *argument) {
  auto *client = static_cast<StarlinkClient *>(argument);
  client->worker_loop();
  client->worker_task_ = nullptr;
  vTaskDelete(nullptr);
}

void StarlinkClient::worker_loop() {
  while (!this->stop_worker_.load(std::memory_order_acquire)) {
    const uint32_t now = millis();
    const int gps_request = this->gps_inhibit_requested_.exchange(-1);
    const bool manual = this->probe_requested_.exchange(false);
    const bool due = time_reached(now, this->next_refresh_ms_);

    if (gps_request >= 0 && this->enabled() && this->configured()) {
      this->perform_gps_inhibit_(gps_request != 0);
      this->next_refresh_ms_ = millis() + this->refresh_interval_ms_;
    } else if ((manual || due) && this->enabled() && this->configured()) {
      this->perform_probe_(!manual);
      this->next_refresh_ms_ = millis() + this->refresh_interval_ms_;
    } else if (due) {
      this->next_refresh_ms_ = now + this->refresh_interval_ms_;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void StarlinkClient::perform_probe_(bool scheduled) {
  if (!network::is_connected()) {
    this->commit_result_(false, 0, false, false, false, 0, -1);
    return;
  }

  DishGpsStatus gps{};
  const auto result = dish_get_status(
      this->host_copy_(), this->port(), this->connect_timeout_ms_, &gps);
  const bool ok = result.outcome == RpcOutcome::OK && gps.parsed;
  this->commit_result_(ok, result.latency_ms, gps.parsed, gps.gps_valid,
                       gps.inhibit_gps, gps.gps_sats, result.grpc_status);
  ESP_LOGI(TAG,
           "Dishy status %s grpc=%d latency=%lu ms gps_valid=%s sats=%lu "
           "inhibit=%s scheduled=%s",
           outcome_name(result.outcome), result.grpc_status,
           static_cast<unsigned long>(result.latency_ms), YESNO(gps.gps_valid),
           static_cast<unsigned long>(gps.gps_sats), YESNO(gps.inhibit_gps),
           YESNO(scheduled));
}

void StarlinkClient::perform_gps_inhibit_(bool inhibit) {
  if (!network::is_connected()) {
    this->commit_command_error_(-1, 0);
    return;
  }

  const auto result = dish_set_gps_inhibit(
      this->host_copy_(), this->port(), this->connect_timeout_ms_, inhibit);
  ESP_LOGI(TAG, "Dishy GPS inhibit=%s result=%s grpc=%d latency=%lu ms",
           YESNO(inhibit), outcome_name(result.outcome), result.grpc_status,
           static_cast<unsigned long>(result.latency_ms));
  if (result.outcome != RpcOutcome::OK) {
    this->commit_command_error_(result.grpc_status, result.latency_ms);
    return;
  }

  // Read back the state instead of optimistically publishing the requested
  // value. This keeps Web UI and firmware aligned with Dishy itself.
  this->perform_probe_(false);
}

std::string StarlinkClient::host_copy_() const {
  std::lock_guard<std::mutex> lock(this->host_mutex_);
  return this->host_;
}

void StarlinkClient::commit_result_(bool reachable, uint32_t latency_ms,
                                    bool gps_status_valid, bool gps_valid,
                                    bool inhibit_gps, uint32_t gps_satellites,
                                    int grpc_status) {
  {
    std::lock_guard<std::mutex> lock(this->state_mutex_);
    this->snapshot_.valid = true;
    this->snapshot_.reachable = reachable;
    this->snapshot_.gps_status_valid = gps_status_valid;
    this->snapshot_.gps_valid = gps_valid;
    this->snapshot_.inhibit_gps = inhibit_gps;
    this->snapshot_.gps_satellites = gps_satellites;
    this->snapshot_.grpc_status = grpc_status;
    this->snapshot_.latency_ms = latency_ms;
    this->snapshot_.sequence++;
    if (reachable) this->snapshot_.errors = 0;
    else this->snapshot_.errors++;
  }
  this->update_pending_.store(true, std::memory_order_release);
}

void StarlinkClient::commit_command_error_(int grpc_status,
                                           uint32_t latency_ms) {
  {
    std::lock_guard<std::mutex> lock(this->state_mutex_);
    this->snapshot_.grpc_status = grpc_status;
    this->snapshot_.latency_ms = latency_ms;
    this->snapshot_.errors++;
    this->snapshot_.sequence++;
  }
  this->update_pending_.store(true, std::memory_order_release);
}

}  // namespace esphome::usp_starlink
