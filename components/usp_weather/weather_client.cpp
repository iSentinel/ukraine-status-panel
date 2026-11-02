#include "weather_client.h"
#include "usp_weather_codes.h"
#include "usp_weather_time.h"
#include "usp_weather_timezone.h"

#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/components/usp_weather/network_gate.h"
#include "esphome/core/log.h"
#include "esphome/core/defines.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <utility>

namespace esphome::usp_weather {

static const char *const TAG = "usp.weather";
static constexpr size_t MAX_HTTP_BODY = 16384;
static constexpr char USER_AGENT[] =
    "UkraineStatusPanel/" ESPHOME_PROJECT_VERSION;

namespace {

struct HttpBodyCollector {
  char *body{nullptr};
  size_t capacity{0};
  size_t size{0};
  bool overflow{false};
  usp_http_retry_after::HeaderCapture retry_after{};
};

struct PsramHttpBody {
  PsramHttpBody() {
    this->data = static_cast<char *>(heap_caps_malloc(
        MAX_HTTP_BODY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  ~PsramHttpBody() {
    if (this->data != nullptr) heap_caps_free(this->data);
  }
  PsramHttpBody(const PsramHttpBody &) = delete;
  PsramHttpBody &operator=(const PsramHttpBody &) = delete;

  char *data{nullptr};
};

bool time_reached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

template<size_t N> void copy_text(char (&target)[N], const std::string &value) {
  size_t count = std::min(value.size(), N - 1);
  while (count != 0 && (static_cast<unsigned char>(value.c_str()[count]) & 0xC0U) == 0x80U) --count;
  std::memcpy(target, value.data(), count);
  target[count] = '\0';
}

std::string safe_text(JsonVariantConst value, size_t maximum = MAX_HTTP_BODY) {
  // Keep the JSON length so validation sees embedded control bytes.
  const JsonString text = value.as<JsonString>();
  if (text.isNull() || text.size() > maximum) return {};
  return std::string{text.c_str(), text.size()};
}

esp_err_t collect_http_body(esp_http_client_event_t *event) {
  auto *collector = static_cast<HttpBodyCollector *>(event->user_data);
  if (collector == nullptr) return ESP_OK;
  if (event->event_id == HTTP_EVENT_ON_HEADER) {
    collector->retry_after.observe(event->header_key, event->header_value);
    return ESP_OK;
  }
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
    return ESP_OK;
  }
  const size_t data_len = static_cast<size_t>(event->data_len);
  if (collector->body == nullptr || collector->size > collector->capacity ||
      data_len > collector->capacity - collector->size) {
    collector->overflow = true;
    return ESP_FAIL;
  }
  std::memcpy(collector->body + collector->size, event->data, data_len);
  collector->size += data_len;
  return ESP_OK;
}

bool finite_number(JsonVariantConst value, double *out) {
  if (value.is<bool>() || !value.is<double>()) return false;
  *out = value.as<double>();
  return std::isfinite(*out);
}

bool finite_float(JsonVariantConst value, float *out) {
  double number;
  if (!finite_number(value, &number)) return false;
  *out = static_cast<float>(number);
  return std::isfinite(*out) && static_cast<double>(*out) >= std::numeric_limits<int32_t>::min() &&
         static_cast<double>(*out) <= std::numeric_limits<int32_t>::max();
}

bool byte_number(JsonVariantConst value, uint8_t *out, uint8_t maximum = 255) {
  double number;
  if (!finite_number(value, &number) || number < 0 || number > maximum ||
      std::floor(number) != number) return false;
  *out = static_cast<uint8_t>(number);
  return true;
}

bool valid_date(const std::string &date) {
  return usp_weather_time::valid_date(date);
}

bool valid_timestamp(const std::string &text) {
  return usp_weather_time::valid_timestamp(text);
}

bool valid_text(const std::string &text, bool allow_empty = false) {
  return (allow_empty || !text.empty()) &&
         std::none_of(text.begin(), text.end(), [](unsigned char ch) { return ch < 32 || ch == 127; });
}

}  // namespace

void WeatherClient::setup() {
  this->city_pref_ =
      global_preferences->make_preference<StoredCity>(CITY_PREF_KEY, true);
  this->load_city_preference_();
  this->next_refresh_ms_ = millis() + this->initial_delay_ms_;

  // Place the HTTPS worker stack in PSRAM. RSA certificate verification
  // needs a sufficiently large contiguous internal-DRAM block; keeping this
  // 12 KiB stack internal can starve MbedTLS under multi-client HTTPS load.
  const BaseType_t result = xTaskCreatePinnedToCoreWithCaps(
      worker_entry, "usp_weather", 12288, this, 1, &this->worker_task_, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (result != pdPASS) {
    this->worker_task_ = nullptr;
    this->mark_failed();
    ESP_LOGE(TAG, "Weather worker could not start");
    return;
  }
  ESP_LOGI(TAG, "Weather worker started");
}

void WeatherClient::loop() {
  if (this->update_pending_.exchange(false)) {
    this->update_trigger_.trigger();
    this->log_snapshot_();
  }
  if (this->search_publish_pending_.exchange(false)) {
    this->search_trigger_.trigger();
  }
  const bool stale = this->weather_stale();
  if (stale != this->last_stale_state_) {
    this->last_stale_state_ = stale;
    this->update_trigger_.trigger();
    ESP_LOGI(TAG, "Weather stale state=%s", YESNO(stale));
  }
}

void WeatherClient::dump_config() {
  ESP_LOGCONFIG(TAG, "Open-Meteo Weather Client");
  ESP_LOGCONFIG(TAG, "  City: %s", this->city_name().c_str());
  ESP_LOGCONFIG(
      TAG, "  Refresh: %lu ms",
      static_cast<unsigned long>(this->refresh_interval_ms_));
  ESP_LOGCONFIG(
      TAG, "  Stale after: %lu ms",
      static_cast<unsigned long>(this->stale_after_ms_));
  ESP_LOGCONFIG(TAG, "  Worker: %s", YESNO(this->worker_task_ != nullptr));
  ESP_LOGCONFIG(
      TAG, "  Minimum forecast attempt interval: %lu ms (device policy)",
      static_cast<unsigned long>(usp_weather_refresh::MIN_ATTEMPT_INTERVAL_MS));
}

void WeatherClient::request_refresh() {
  this->refresh_requested_.store(true, std::memory_order_release);
}

void WeatherClient::dump_runtime() const {
  this->log_snapshot_();
  const StoredCity city = this->selected_city_copy_();
  ESP_LOGI(TAG, "WEATHER_CITY name=%s country=%s timezone=%s lat=%.5f lon=%.5f",
           city.name, city.country, city.timezone, city.latitude, city.longitude);
  std::lock_guard<std::mutex> lock(this->search_mutex_);
  for (size_t index = 0; index < SEARCH_RESULTS; ++index) {
    const auto &item = this->search_results_[index];
    if (!item.valid) {
      continue;
    }
    ESP_LOGI(TAG, "WEATHER_SEARCH index=%u id=%ld name=%s country=%s",
             static_cast<unsigned>(index), static_cast<long>(item.id),
             item.name.c_str(), item.country.c_str());
  }
}

bool WeatherClient::request_city_search(
    const std::string &query, uint8_t language) {
  const size_t first = query.find_first_not_of(" \t\r\n");
  const size_t last = query.find_last_not_of(" \t\r\n");
  const std::string normalized = first == std::string::npos ? "" : query.substr(first, last - first + 1);
  const bool valid = normalized.size() >= 2 && normalized.size() <= 80 && valid_text(normalized);
  {
    std::lock_guard<std::mutex> query_lock(this->query_mutex_);
    std::lock_guard<std::mutex> search_lock(this->search_mutex_);
    ++this->query_generation_;
    this->pending_query_ = valid ? normalized : "";
    this->pending_search_language_ = language < 3 ? language : 2;
    this->search_results_.fill(SearchResult{});
    this->search_requested_.store(valid, std::memory_order_release);
  }
  this->search_publish_pending_.store(true, std::memory_order_release);
  if (!valid) ESP_LOGW(TAG, "City search rejected: invalid query");
  return valid;
}

bool WeatherClient::select_search_result(int32_t index) {
  std::lock_guard<std::mutex> query_lock(this->query_mutex_);
  SearchResult result;
  {
    std::lock_guard<std::mutex> search_lock(this->search_mutex_);
    if (index < 0 || index >= static_cast<int32_t>(SEARCH_RESULTS) ||
        this->published_query_generation_ != this->query_generation_) return false;
    result = this->search_results_[static_cast<size_t>(index)];
  }
  if (!result.valid) return false;
  const StoredCity next = this->city_from_search_result_(result);
  const StoredCity current = this->selected_city_copy_();
  if (this->same_city_(current, next)) {
    this->request_refresh();
    return true;
  }
  {
    // Lock order is always city then weather state.
    std::lock_guard<std::mutex> city_lock(this->city_mutex_);
    std::lock_guard<std::mutex> state_lock(this->state_mutex_);
    this->selected_city_ = next;
    ++this->city_generation_;
    const uint32_t sequence = this->snapshot_.sequence + 1;
    this->snapshot_ = WeatherSnapshot{};
    this->snapshot_.city = next.name;
    this->snapshot_.country = next.country;
    this->snapshot_.sequence = sequence;
  }
  this->update_pending_.store(true, std::memory_order_release);
  this->save_city_preference_();
  this->request_refresh();
  ESP_LOGI(TAG, "Selected weather city: %s", next.name);
  return true;
}

template<typename View>
View WeatherClient::copy_snapshot_() const {
  // Match the writer lock order so a city switch cannot mix generations.
  std::lock_guard<std::mutex> city_lock(this->city_mutex_);
  std::lock_guard<std::mutex> state_lock(this->state_mutex_);
  if (this->snapshot_.valid &&
      millis() - this->snapshot_.received_ms > this->stale_after_ms_)
    this->snapshot_.stale_latched = true;
  View view = static_cast<const View &>(this->snapshot_);
  view.city = this->selected_city_.name;
  view.country = this->selected_city_.country;
  view.stale_latched = !view.valid || view.stale_latched;
  return view;
}
WeatherClient::WeatherSnapshot WeatherClient::snapshot() const {
  return this->copy_snapshot_<WeatherSnapshot>();
}

WeatherClient::WeatherCurrent WeatherClient::current_snapshot() const {
  return this->copy_snapshot_<WeatherCurrent>();
}

bool WeatherClient::weather_valid() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.valid;
}

bool WeatherClient::weather_stale() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (!this->snapshot_.valid) {
    return true;
  }
  if (millis() - this->snapshot_.received_ms > this->stale_after_ms_)
    this->snapshot_.stale_latched = true;
  return this->snapshot_.stale_latched;
}

bool WeatherClient::last_fetch_ok() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.fetch_ok;
}

std::string WeatherClient::city_name() const {
  std::lock_guard<std::mutex> lock(this->city_mutex_);
  return this->selected_city_.name;
}

std::string WeatherClient::country_name() const {
  std::lock_guard<std::mutex> lock(this->city_mutex_);
  return this->selected_city_.country;
}

std::string WeatherClient::updated_time() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.updated_time;
}

float WeatherClient::temperature_c() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.temperature_c;
}

float WeatherClient::apparent_temperature_c() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.apparent_temperature_c;
}

uint8_t WeatherClient::humidity_percent() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.humidity_percent;
}

uint8_t WeatherClient::weather_code() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.weather_code;
}

float WeatherClient::wind_kmh() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.wind_kmh;
}

bool WeatherClient::is_day() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.is_day;
}

std::string WeatherClient::condition_text(uint8_t language) const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return condition_text_(this->snapshot_.weather_code, language);
}

size_t WeatherClient::forecast_count() const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->snapshot_.forecast_count;
}

std::string WeatherClient::forecast_condition_text(
    size_t index, uint8_t language) const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (index >= this->snapshot_.forecast_count) {
    return {};
  }
  return condition_text_(this->snapshot_.days[index].code, language);
}

std::string WeatherClient::forecast_date(size_t index) const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  if (index >= this->snapshot_.forecast_count) {
    return {};
  }
  return this->snapshot_.days[index].date;
}

uint8_t WeatherClient::forecast_code(size_t index) const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return index < this->snapshot_.forecast_count
             ? this->snapshot_.days[index].code
             : 0;
}

float WeatherClient::forecast_high_c(size_t index) const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return index < this->snapshot_.forecast_count
             ? this->snapshot_.days[index].high_c
             : 0.0F;
}

float WeatherClient::forecast_low_c(size_t index) const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return index < this->snapshot_.forecast_count
             ? this->snapshot_.days[index].low_c
             : 0.0F;
}

uint8_t WeatherClient::forecast_precip_percent(size_t index) const {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return index < this->snapshot_.forecast_count
             ? this->snapshot_.days[index].precip_percent
             : 0;
}

size_t WeatherClient::search_result_count() const {
  std::lock_guard<std::mutex> lock(this->search_mutex_);
  size_t count = 0;
  for (const auto &result : this->search_results_) {
    count += result.valid ? 1U : 0U;
  }
  return count;
}

std::string WeatherClient::search_result_label(size_t index) const {
  std::lock_guard<std::mutex> lock(this->search_mutex_);
  if (index >= SEARCH_RESULTS || !this->search_results_[index].valid) {
    return {};
  }
  const auto &item = this->search_results_[index];
  std::string label = item.name;
  if (!item.admin1.empty() && item.admin1 != item.name) {
    label += ", " + item.admin1;
  }
  if (!item.country.empty()) {
    label += ", " + item.country;
  }
  return label;
}

void WeatherClient::worker_entry(void *argument) {
  auto *client = static_cast<WeatherClient *>(argument);
  client->worker_loop();
  vTaskDelete(nullptr);
}

void WeatherClient::worker_loop() {
  while (!this->stop_worker_.load(std::memory_order_acquire)) {
    uint32_t now = millis();
    const bool search_pending =
        this->search_requested_.load(std::memory_order_acquire);
    if (search_pending && this->search_retry_gate_.ready(now) &&
        this->search_requested_.exchange(false, std::memory_order_acq_rel)) {
      this->process_search_request();
    }
    now = millis();
    const bool manual_pending =
        this->refresh_requested_.load(std::memory_order_acquire);
    const bool scheduled_due = time_reached(now, this->next_refresh_ms_);
    if (this->forecast_retry_gate_.ready(now) &&
        usp_weather_refresh::should_start(
            manual_pending, scheduled_due, this->forecast_attempt_state_, now)) {
      bool manual = false;
      if (manual_pending) {
        manual = this->refresh_requested_.exchange(
            false, std::memory_order_acq_rel);
      }
      usp_weather_refresh::mark_started(&this->forecast_attempt_state_, now);
      this->process_forecast_request(!manual);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

WeatherClient::SearchRequest WeatherClient::search_request_copy_() const {
  std::lock_guard<std::mutex> lock(this->query_mutex_);
  return {this->pending_query_, this->query_generation_,
          this->pending_search_language_};
}

void WeatherClient::process_search_request() {
  const SearchRequest request = this->search_request_copy_();
  if (request.query.empty()) return;
  PsramHttpBody body;
  size_t body_size = 0;
  int status = 0;
  usp_http_retry_after::HeaderCapture retry_after;
  std::array<SearchResult, SEARCH_RESULTS> parsed{};
  const bool fetched =
      body.data != nullptr &&
      this->fetch_json_(this->build_search_url_(request.query, request.language),
                        body.data, MAX_HTTP_BODY, &body_size, &status,
                        &retry_after);
  if (status == 429) {
    const uint32_t delay = this->search_retry_gate_.apply(
        status, retry_after, MIN_RETRY_MS, millis());
    this->search_requested_.store(true, std::memory_order_release);
    ESP_LOGW(TAG, "City search rate limited retry_after_ms=%lu",
             static_cast<unsigned long>(delay));
  }
  const bool ok = fetched &&
                  this->parse_search_(
                      reinterpret_cast<const uint8_t *>(body.data), body_size,
                      &parsed);
  if (!this->publish_search_(request, std::move(parsed))) {
    ESP_LOGI(TAG, "Discarded superseded city search reply");
    return;
  }
  ESP_LOGI(TAG, "City search finished results=%u ok=%s",
           static_cast<unsigned>(this->search_result_count()), YESNO(ok));
}

bool WeatherClient::publish_search_(const SearchRequest &request,
    std::array<SearchResult, SEARCH_RESULTS> parsed) {
  std::lock_guard<std::mutex> query_lock(this->query_mutex_);
  if (request.generation != this->query_generation_) return false;
  std::lock_guard<std::mutex> search_lock(this->search_mutex_);
  this->search_results_ = std::move(parsed);
  this->published_query_generation_ = request.generation;
  this->search_publish_pending_.store(true, std::memory_order_release);
  return true;
}

WeatherClient::ForecastRequest WeatherClient::forecast_request_copy_() const {
  std::lock_guard<std::mutex> lock(this->city_mutex_);
  return {this->selected_city_, this->city_generation_};
}

void WeatherClient::process_forecast_request(bool scheduled) {
  const ForecastRequest request = this->forecast_request_copy_();
  PsramHttpBody body;
  size_t body_size = 0;
  int status = 0;
  usp_http_retry_after::HeaderCapture retry_after;
  WeatherSnapshot next;
  const bool fetched =
      body.data != nullptr &&
      this->fetch_json_(this->build_forecast_url_(request.city), body.data,
                        MAX_HTTP_BODY, &body_size, &status, &retry_after);
  if (status == 429) {
    const uint32_t delay = this->forecast_retry_gate_.apply(
        status, retry_after, this->retry_delay_ms_, millis());
    ESP_LOGW(TAG, "Forecast rate limited retry_after_ms=%lu",
             static_cast<unsigned long>(delay));
  }
  const bool parsed = fetched &&
                      this->parse_forecast_(
                          reinterpret_cast<const uint8_t *>(body.data), body_size,
                          &next);
  if (!this->publish_forecast_(request, std::move(next), parsed)) {
    ESP_LOGI(TAG, "Discarded forecast reply for superseded city");
    return;
  }
  if (parsed) this->schedule_success_();
  else this->schedule_retry_();
  ESP_LOGI(TAG, "Forecast request %s ok=%s", scheduled ? "scheduled" : "manual", YESNO(parsed));
}

bool WeatherClient::publish_forecast_(const ForecastRequest &request,
                                    WeatherSnapshot next, bool valid) {
  std::lock_guard<std::mutex> city_lock(this->city_mutex_);
  if (request.generation != this->city_generation_) return false;
  std::lock_guard<std::mutex> state_lock(this->state_mutex_);
  if (valid) {
    next.city = request.city.name;
    next.country = request.city.country;
    next.sequence = this->snapshot_.sequence + 1;
    this->snapshot_ = std::move(next);
  } else {
    this->snapshot_.fetch_ok = false;
  }
  this->update_pending_.store(true, std::memory_order_release);
  return true;
}

bool WeatherClient::fetch_json_(
    const std::string &url, char *body, size_t capacity, size_t *length,
    int *status_out, usp_http_retry_after::HeaderCapture *retry_after_out) {
  if (status_out != nullptr) *status_out = 0;
  if (retry_after_out != nullptr) retry_after_out->reset();
  if (!network::is_connected()) {
    ESP_LOGW(TAG, "Weather request skipped: network is offline");
    return false;
  }
  usp_network::HttpsTransaction network_tx(
      "weather", usp_network::HttpsPriority::NORMAL, 2500);
  if (!network_tx.acquired()) {
    ESP_LOGW(TAG, "Weather request skipped: HTTPS gate busy");
    return false;
  }
  if (body == nullptr || capacity == 0 || length == nullptr) return false;
  *length = 0;
  HttpBodyCollector collector{body, capacity, 0, false, {}};
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = 8000;
  config.user_agent = USER_AGENT;
  config.event_handler = collect_http_body;
  config.user_data = &collector;
  config.buffer_size = 2048;
  config.buffer_size_tx = 1024;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
  config.crt_bundle_attach = esp_crt_bundle_attach;
#endif
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    ESP_LOGW(TAG, "Weather HTTP client initialization failed");
    return false;
  }
  esp_http_client_set_header(client, "Accept", "application/json");
  esp_http_client_set_header(client, "Accept-Encoding", "identity");
  const esp_err_t error = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  if (status_out != nullptr) *status_out = status;
  if (retry_after_out != nullptr) *retry_after_out = collector.retry_after;
  esp_http_client_cleanup(client);
  if (error != ESP_OK || status != 200 || collector.overflow) {
    ESP_LOGW(TAG, "Weather HTTP failed err=%s status=%d overflow=%s",
             esp_err_to_name(error), status, YESNO(collector.overflow));
    return false;
  }
  *length = collector.size;
  return true;
}

bool WeatherClient::parse_search_(
    const uint8_t *body, size_t body_size,
    std::array<SearchResult, SEARCH_RESULTS> *parsed) {
  auto document = json::parse_json(body, body_size);
  if (document.isNull()) return false;
  JsonArrayConst results = document["results"].as<JsonArrayConst>();
  if (results.isNull()) return document["results"].isNull() && document["error"].isNull();
  size_t index = 0;
  for (JsonObjectConst item : results) {
    if (index >= SEARCH_RESULTS) break;
    SearchResult result;
    if (!this->parse_search_item_(item, &result)) continue;
    (*parsed)[index++] = std::move(result);
  }
  return true;
}

bool WeatherClient::parse_search_item_(JsonObjectConst item, SearchResult *result) {
  if (!item["id"].is<int32_t>() || item["id"].as<int32_t>() <= 0 ||
      !finite_number(item["latitude"], &result->latitude) ||
      !finite_number(item["longitude"], &result->longitude) ||
      result->latitude < -90 || result->latitude > 90 ||
      result->longitude < -180 || result->longitude > 180) return false;
  result->id = item["id"].as<int32_t>();
  result->name = safe_text(item["name"]);
  result->admin1 = safe_text(item["admin1"]);
  result->country = safe_text(item["country"]);
  result->country_code = safe_text(item["country_code"]);
  result->timezone = safe_text(item["timezone"]);
  result->valid = valid_text(result->name) && valid_text(result->timezone) &&
                  result->timezone.size() < 48 && valid_text(result->admin1, true) &&
                  valid_text(result->country, true);
  return result->valid;
}

bool WeatherClient::parse_forecast_(const uint8_t *body, size_t body_size,
                                    WeatherSnapshot *next) {
  auto document = json::parse_json(body, body_size);
  if (document.isNull()) return false;
  JsonObjectConst current = document["current"].as<JsonObjectConst>();
  JsonObjectConst daily = document["daily"].as<JsonObjectConst>();
  JsonObjectConst hourly = document["hourly"].as<JsonObjectConst>();
  if (current.isNull() || daily.isNull() || !this->parse_current_(current, next)) {
    ESP_LOGW(TAG, "Forecast payload has missing or invalid required values");
    return false;
  }
  this->parse_time_context_(document.as<JsonObjectConst>(), next);
  this->fill_forecast_days_(daily, next);
  this->fill_forecast_hours_(hourly, next->updated_time, next);
  next->valid = true;
  next->fetch_ok = true;
  next->received_ms = millis();
  return true;
}

bool WeatherClient::parse_current_(JsonObjectConst current, WeatherSnapshot *next) {
  uint8_t day;
  next->updated_time = safe_text(current["time"]);
  if (!valid_timestamp(next->updated_time) ||
      !finite_float(current["temperature_2m"], &next->temperature_c) ||
      !finite_float(current["apparent_temperature"], &next->apparent_temperature_c) ||
      !finite_float(current["wind_speed_10m"], &next->wind_kmh) || next->wind_kmh < 0 ||
      !byte_number(current["relative_humidity_2m"], &next->humidity_percent, 100) ||
      !byte_number(current["weather_code"], &next->weather_code) ||
      !byte_number(current["is_day"], &day, 1)) return false;
  next->is_day = day != 0;
  return true;
}

void WeatherClient::parse_time_context_(
    JsonObjectConst root, WeatherSnapshot *snapshot) {
  // Missing metadata never changes the validity of the measured weather.
  snapshot->timezone.clear();
  snapshot->utc_offset_seconds = 0;
  snapshot->time_context_valid = false;
  std::string zone = safe_text(root["timezone"], 63);
  double seconds;
  if (!usp_weather_timezone::valid_name(zone) ||
      !finite_number(root["utc_offset_seconds"], &seconds) ||
      std::floor(seconds) != seconds || seconds < -43200 || seconds > 50400 ||
      !usp_weather_timezone::valid_offset(static_cast<int32_t>(seconds))) return;
  snapshot->timezone = std::move(zone);
  snapshot->utc_offset_seconds = static_cast<int32_t>(seconds);
  snapshot->time_context_valid = true;
}

void WeatherClient::fill_forecast_days_(JsonObjectConst daily, WeatherSnapshot *snapshot) {
  JsonArrayConst dates = daily["time"].as<JsonArrayConst>();
  JsonArrayConst codes = daily["weather_code"].as<JsonArrayConst>();
  JsonArrayConst highs = daily["temperature_2m_max"].as<JsonArrayConst>();
  JsonArrayConst lows = daily["temperature_2m_min"].as<JsonArrayConst>();
  JsonArrayConst rain = daily["precipitation_probability_max"].as<JsonArrayConst>();
  JsonArrayConst sums = daily["precipitation_sum"].as<JsonArrayConst>();
  JsonArrayConst sunrises = daily["sunrise"].as<JsonArrayConst>();
  JsonArrayConst sunsets = daily["sunset"].as<JsonArrayConst>();
  JsonArrayConst uv = daily["uv_index_max"].as<JsonArrayConst>();
  const size_t count = std::min({FORECAST_DAYS, dates.size(), codes.size(), highs.size(), lows.size(), rain.size()});
  snapshot->forecast_count = 0;
  for (size_t index = 0; index < count; ++index) {
    ForecastDay day;
    const std::string date = safe_text(dates[index]);
    if (!valid_date(date) || !byte_number(codes[index], &day.code) ||
        !finite_float(highs[index], &day.high_c) || !finite_float(lows[index], &day.low_c) ||
        day.low_c > day.high_c || !byte_number(rain[index], &day.precip_percent, 100)) continue;
    if (snapshot->forecast_count != 0 &&
        date <= snapshot->days[snapshot->forecast_count - 1].date) continue;
    copy_text(day.date, date);
    if (!finite_float(sums[index], &day.precip_sum_mm) || day.precip_sum_mm < 0)
      day.precip_sum_mm = std::numeric_limits<float>::quiet_NaN();
    if (!finite_float(uv[index], &day.uv_max) || day.uv_max < 0)
      day.uv_max = std::numeric_limits<float>::quiet_NaN();
    if (index < sunrises.size()) {
      const std::string value = safe_text(sunrises[index]);
      if (valid_timestamp(value)) copy_text(day.sunrise, value.substr(11, 5));
    }
    if (index < sunsets.size()) {
      const std::string value = safe_text(sunsets[index]);
      if (valid_timestamp(value)) copy_text(day.sunset, value.substr(11, 5));
    }
    snapshot->days[snapshot->forecast_count++] = day;
  }
}

bool WeatherClient::parse_forecast_hour_(
    JsonObjectConst hourly, size_t index, ForecastHour *hour) {
  // Preserve the core hour even when a secondary detail is unavailable.
  uint8_t is_day = 0;
  if (!finite_float(hourly["temperature_2m"][index], &hour->temperature_c) ||
      !byte_number(hourly["precipitation_probability"][index],
                   &hour->precip_percent, 100) ||
      !byte_number(hourly["weather_code"][index], &hour->code) ||
      !byte_number(hourly["is_day"][index], &is_day, 1)) return false;
  hour->is_day = is_day != 0;
  auto optional = [&](const char *key, float *value, float low, float high) {
    float parsed = std::numeric_limits<float>::quiet_NaN();
    if (finite_float(hourly[key][index], &parsed) && parsed >= low && parsed <= high)
      *value = parsed;
  };
  optional("apparent_temperature", &hour->apparent_temperature_c, -200.0F, 200.0F);
  optional("precipitation", &hour->precip_mm, 0.0F, 10000.0F);
  optional("wind_speed_10m", &hour->wind_kmh, 0.0F, 1000.0F);
  optional("wind_direction_10m", &hour->wind_direction_deg, 0.0F, 360.0F);
  optional("wind_gusts_10m", &hour->gust_kmh, 0.0F, 1000.0F);
  optional("surface_pressure", &hour->pressure_hpa, 1.0F, 2000.0F);
  optional("uv_index", &hour->uv_index, 0.0F, 100.0F);
  return true;
}

void WeatherClient::fill_forecast_hours_(
    JsonObjectConst hourly, const std::string &current_time,
    WeatherSnapshot *snapshot) {
  snapshot->hourly_count = 0;
  JsonArrayConst times = hourly["time"].as<JsonArrayConst>();
  // Limit CPU work even if a malformed provider reply contains extra rows.
  const size_t count = std::min(times.size(), FORECAST_HOURS + 1);
  for (size_t index = 0; index < count && snapshot->hourly_count < FORECAST_HOURS; ++index) {
    ForecastHour hour;
    const std::string time = safe_text(times[index]);
    if (time.size() != 16 || !valid_timestamp(time) || time < current_time ||
        !this->parse_forecast_hour_(hourly, index, &hour)) continue;
    if (snapshot->hourly_count != 0 &&
        time <= snapshot->hours[snapshot->hourly_count - 1].time) continue;
    copy_text(hour.time, time);
    snapshot->hours[snapshot->hourly_count++] = hour;
  }
}

std::string WeatherClient::build_search_url_(
    const std::string &query, uint8_t language) const {
  static constexpr const char *LANGUAGES[] = {"uk", "ru", "en"};
  const uint8_t index = language < 3 ? language : 2;
  return "https://geocoding-api.open-meteo.com/v1/search?name=" +
         this->url_encode_(query) + "&count=3&language=" +
         LANGUAGES[index] + "&format=json";
}

std::string WeatherClient::build_forecast_url_(const StoredCity &city) const {
  char location[96];
  std::snprintf(location, sizeof(location), "latitude=%.6f&longitude=%.6f",
                city.latitude, city.longitude);
  return std::string("https://api.open-meteo.com/v1/forecast?") + location +
         "&current=temperature_2m,apparent_temperature,relative_humidity_2m,"
         "weather_code,is_day,wind_speed_10m&hourly=temperature_2m,apparent_temperature,"
         "precipitation_probability,precipitation,weather_code,is_day,wind_speed_10m,"
         "wind_direction_10m,wind_gusts_10m,surface_pressure,uv_index&daily=weather_code,"
         "temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
         "precipitation_sum,sunrise,sunset,uv_index_max&timezone=auto&forecast_days=7&forecast_hours=25"
         "&temperature_unit=celsius&wind_speed_unit=kmh";
}

std::string WeatherClient::url_encode_(const std::string &value) {
  static constexpr char HEX[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(value.size() * 3);
  for (const unsigned char ch : value) {
    const bool safe = std::isalnum(ch) || ch == '-' || ch == '_' ||
                      ch == '.' || ch == '~';
    if (safe) {
      encoded.push_back(static_cast<char>(ch));
      continue;
    }
    encoded.push_back('%');
    encoded.push_back(HEX[(ch >> 4U) & 0x0FU]);
    encoded.push_back(HEX[ch & 0x0FU]);
  }
  return encoded;
}

void WeatherClient::load_city_preference_() {
  StoredCity restored{};
  if (!this->city_pref_.load(&restored) || restored.magic != CITY_MAGIC) {
    this->use_default_city_();
    ESP_LOGI(TAG, "Weather city preference unset; default=Kyiv");
    return;
  }
  if (!this->valid_stored_city_(restored)) {
    this->use_default_city_();
    ESP_LOGW(TAG, "Weather city preference invalid; default=Kyiv");
    return;
  }
  {
    std::lock_guard<std::mutex> lock(this->city_mutex_);
    this->selected_city_ = restored;
  }
  ESP_LOGI(TAG, "Weather city restored: %s", restored.name);
}

bool WeatherClient::save_city_preference_() {
  const StoredCity city = this->selected_city_copy_();
  // Do not flush the global preference queue from an interactive location
  // change. The ESPHome IntervalSyncer persists pending values every 60 s and
  // on shutdown without blocking this Web/API callback.
  if (!this->city_pref_.save(&city)) {
    ESP_LOGW(TAG, "Weather city preference queue failed");
    return false;
  }
  ESP_LOGI(TAG, "Weather city queued: %s", city.name);
  return true;
}

bool WeatherClient::valid_stored_city_(const StoredCity &city) const {
  return city.magic == CITY_MAGIC && std::isfinite(city.latitude) && std::isfinite(city.longitude) &&
         city.latitude >= -90 && city.latitude <= 90 && city.longitude >= -180 && city.longitude <= 180 &&
         city.name[0] != '\0' && std::memchr(city.name, 0, sizeof(city.name)) != nullptr &&
         std::memchr(city.country, 0, sizeof(city.country)) != nullptr &&
         std::memchr(city.timezone, 0, sizeof(city.timezone)) != nullptr;
}

void WeatherClient::use_default_city_() {
  StoredCity city{};
  city.magic = CITY_MAGIC;
  city.latitude = 50.45466;
  city.longitude = 30.52380;
  copy_text(city.name, "Kyiv");
  copy_text(city.country, "Ukraine");
  copy_text(city.timezone, "Europe/Kyiv");
  std::lock_guard<std::mutex> lock(this->city_mutex_);
  this->selected_city_ = city;
}

WeatherClient::StoredCity WeatherClient::selected_city_copy_() const {
  std::lock_guard<std::mutex> lock(this->city_mutex_);
  return this->selected_city_;
}

WeatherClient::StoredCity WeatherClient::city_from_search_result_(
    const SearchResult &result) const {
  StoredCity city{};
  city.magic = CITY_MAGIC;
  city.latitude = result.latitude;
  city.longitude = result.longitude;
  copy_text(city.name, result.name);
  copy_text(city.country, result.country);
  copy_text(city.timezone, result.timezone);
  return city;
}

bool WeatherClient::same_city_(const StoredCity &left,
                               const StoredCity &right) const {
  return left.magic == right.magic && left.latitude == right.latitude &&
         left.longitude == right.longitude &&
         std::strcmp(left.name, right.name) == 0 &&
         std::strcmp(left.country, right.country) == 0 &&
         std::strcmp(left.timezone, right.timezone) == 0;
}

void WeatherClient::schedule_retry_() {
  const uint32_t delay = this->retry_delay_ms_;
  this->next_refresh_ms_ = millis() + delay;
  this->retry_delay_ms_ =
      std::min(this->retry_delay_ms_ * 2U, MAX_RETRY_MS);
  ESP_LOGI(
      TAG, "Weather retry in %lu ms",
      static_cast<unsigned long>(delay));
}

void WeatherClient::schedule_success_() {
  this->retry_delay_ms_ = MIN_RETRY_MS;
  this->next_refresh_ms_ = millis() + this->refresh_interval_ms_;
}

uint8_t WeatherClient::condition_group_(uint8_t code) {
  return usp_weather_codes::group(code);
}

std::string WeatherClient::condition_text_(
    uint8_t code, uint8_t language) {
  return usp_weather_codes::description(code, language);
}

uint32_t WeatherClient::worker_free_stack_bytes_() const {
  if (this->worker_task_ == nullptr) return 0;
  return static_cast<uint32_t>(
      uxTaskGetStackHighWaterMark(this->worker_task_) * sizeof(StackType_t));
}

void WeatherClient::log_snapshot_() const {
  const WeatherCurrent snapshot = this->current_snapshot();
  ESP_LOGI(TAG,
           "WEATHER city=%s valid=%s stale=%s fetch_ok=%s temp=%.1f "
           "apparent=%.1f humidity=%u code=%u wind=%.1f days=%u seq=%lu stack_free_b=%lu hours=%u tz_ok=%s tz=%s utc_offset_s=%ld",
           snapshot.city.c_str(), YESNO(snapshot.valid),
           YESNO(snapshot.stale_latched), YESNO(snapshot.fetch_ok),
           snapshot.temperature_c, snapshot.apparent_temperature_c,
           snapshot.humidity_percent, snapshot.weather_code, snapshot.wind_kmh,
           static_cast<unsigned>(snapshot.forecast_count),
           static_cast<unsigned long>(snapshot.sequence),
           static_cast<unsigned long>(this->worker_free_stack_bytes_()),
           static_cast<unsigned>(snapshot.hourly_count),
           YESNO(snapshot.time_context_valid),
           snapshot.time_context_valid ? snapshot.timezone.c_str() : "--",
           static_cast<long>(snapshot.utc_offset_seconds));
}

}  // namespace esphome::usp_weather
