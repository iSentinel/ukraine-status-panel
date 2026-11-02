#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include <ArduinoJson.h>

// ESPHome creates global storage before custom includes. Make the plain
// owning view type available there without including any drawing functions.
#include "usp_weather_view_types.h"
#include "usp_forecast_view_types.h"
#include "usp_forecast_details_view.h"
#include "usp_weather_refresh_gate.h"
#include "usp_http_retry_after.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <limits>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome::usp_weather {

class WeatherClient : public Component {
 public:
  static constexpr size_t FORECAST_DAYS = 7;
  static constexpr size_t FORECAST_HOURS = 24;
  static constexpr size_t SEARCH_RESULTS = 3;

  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_refresh_interval(uint32_t value) { refresh_interval_ms_ = value; }
  void set_stale_after(uint32_t value) { stale_after_ms_ = value; }
  void set_initial_delay(uint32_t value) { initial_delay_ms_ = value; }

  Trigger<> *get_update_trigger() { return &update_trigger_; }
  Trigger<> *get_search_trigger() { return &search_trigger_; }

  void request_refresh();
  void dump_runtime() const;
  // Explicit search: 0/1/2 means uk/ru/en; old callers keep English.
  bool request_city_search(const std::string &query, uint8_t language = 2);
  bool select_search_result(int32_t index);

  bool weather_valid() const;
  bool weather_stale() const;
  bool last_fetch_ok() const;
  std::string city_name() const;
  std::string country_name() const;
  std::string updated_time() const;
  float temperature_c() const;
  float apparent_temperature_c() const;
  uint8_t humidity_percent() const;
  uint8_t weather_code() const;
  float wind_kmh() const;
  bool is_day() const;
  std::string condition_text(uint8_t language) const;

  size_t forecast_count() const;
  std::string forecast_condition_text(
      size_t index, uint8_t language) const;
  std::string forecast_date(size_t index) const;
  uint8_t forecast_code(size_t index) const;
  float forecast_high_c(size_t index) const;
  float forecast_low_c(size_t index) const;
  uint8_t forecast_precip_percent(size_t index) const;

  size_t search_result_count() const;
  std::string search_result_label(size_t index) const;

 public:
  // A returned view owns its strings and forecast rows.
  struct ForecastDay {
    char date[11]{};
    char sunrise[6]{};
    char sunset[6]{};
    float high_c{0.0F};
    float low_c{0.0F};
    float precip_sum_mm{std::numeric_limits<float>::quiet_NaN()};
    float uv_max{std::numeric_limits<float>::quiet_NaN()};
    uint8_t code{0};
    uint8_t precip_percent{0};
  };

  struct ForecastHour {
    char time[17]{};
    float temperature_c{0.0F};
    float apparent_temperature_c{std::numeric_limits<float>::quiet_NaN()};
    float precip_mm{std::numeric_limits<float>::quiet_NaN()};
    float wind_kmh{std::numeric_limits<float>::quiet_NaN()};
    float wind_direction_deg{std::numeric_limits<float>::quiet_NaN()};
    float gust_kmh{std::numeric_limits<float>::quiet_NaN()};
    float pressure_hpa{std::numeric_limits<float>::quiet_NaN()};
    float uv_index{std::numeric_limits<float>::quiet_NaN()};
    uint8_t precip_percent{0};
    uint8_t code{0};
    bool is_day{false};
  };

  static_assert(sizeof(ForecastDay) <= 72, "Keep daily weather rows bounded");
  static_assert(sizeof(ForecastDay) * FORECAST_DAYS +
                    sizeof(ForecastHour) * FORECAST_HOURS <= 1728,
                "Keep hourly/daily weather row payload below 1728 bytes");
  static_assert(sizeof(ForecastHour) <= 64, "Keep hourly weather rows bounded");

  struct WeatherCurrent {
    bool valid{false};
    bool fetch_ok{false};
    mutable bool stale_latched{false};
    bool is_day{false};
    uint32_t sequence{0};
    uint32_t received_ms{0};
    std::string city;
    std::string country;
    std::string updated_time;
    // Provider response context; this is never a rule for future DST.
    std::string timezone;
    int32_t utc_offset_seconds{0};
    bool time_context_valid{false};
    float temperature_c{0.0F};
    float apparent_temperature_c{0.0F};
    float wind_kmh{0.0F};
    uint8_t humidity_percent{0};
    uint8_t weather_code{0};
    size_t forecast_count{0};
    size_t hourly_count{0};
  };

  static_assert(sizeof(WeatherCurrent) <= 192, "Keep current weather copies small");

  struct WeatherSnapshot : WeatherCurrent {
    std::array<ForecastDay, FORECAST_DAYS> days{};
    std::array<ForecastHour, FORECAST_HOURS> hours{};
  };

  // Read all displayed weather fields under one consistent lock.
  WeatherSnapshot snapshot() const;
  WeatherCurrent current_snapshot() const;
  static std::string condition_text(uint8_t code, uint8_t language) {
    return condition_text_(code, language);
  }

 protected:
  template<typename View> View copy_snapshot_() const;
  uint32_t worker_free_stack_bytes_() const;

  struct SearchResult {
    bool valid{false};
    int32_t id{0};
    double latitude{0.0};
    double longitude{0.0};
    std::string name;
    std::string admin1;
    std::string country;
    std::string country_code;
    std::string timezone;
  };

  struct StoredCity {
    uint32_t magic{0};
    double latitude{0.0};
    double longitude{0.0};
    char name[64]{};
    char country[48]{};
    char timezone[48]{};
  };

  struct ForecastRequest {
    StoredCity city;
    uint64_t generation;
  };
  struct SearchRequest {
    std::string query;
    uint64_t generation;
    // Keep locale with this query even when a newer UI request arrives.
    uint8_t language{2};
  };

  ForecastRequest forecast_request_copy_() const;
  SearchRequest search_request_copy_() const;
  bool publish_forecast_(const ForecastRequest &request, WeatherSnapshot next, bool valid);
  bool publish_search_(const SearchRequest &request, std::array<SearchResult, SEARCH_RESULTS> parsed);
  bool parse_current_(JsonObjectConst current, WeatherSnapshot *next);
  bool parse_search_item_(JsonObjectConst item, SearchResult *result);
  bool valid_stored_city_(const StoredCity &city) const;

  static void worker_entry(void *argument);
  void worker_loop();
  void process_search_request();
  void process_forecast_request(bool scheduled);

  bool fetch_json_(const std::string &url, char *body, size_t capacity,
                   size_t *length, int *status,
                   usp_http_retry_after::HeaderCapture *retry_after);
  bool parse_search_(
      const uint8_t *body, size_t body_size,
      std::array<SearchResult, SEARCH_RESULTS> *parsed);
  bool parse_forecast_(
      const uint8_t *body, size_t body_size, WeatherSnapshot *next);
  void parse_time_context_(JsonObjectConst root, WeatherSnapshot *snapshot);
  void fill_forecast_days_(
      JsonObjectConst daily, WeatherSnapshot *snapshot);
  bool parse_forecast_hour_(JsonObjectConst hourly, size_t index, ForecastHour *hour);
  void fill_forecast_hours_(
      JsonObjectConst hourly, const std::string &current_time, WeatherSnapshot *snapshot);

  std::string build_search_url_(const std::string &query, uint8_t language = 2) const;
  std::string build_forecast_url_(const StoredCity &city) const;
  static std::string url_encode_(const std::string &value);

  void load_city_preference_();
  bool save_city_preference_();
  void use_default_city_();
  StoredCity selected_city_copy_() const;
  StoredCity city_from_search_result_(const SearchResult &result) const;
  bool same_city_(const StoredCity &left, const StoredCity &right) const;
  void schedule_retry_();
  void schedule_success_();
  void log_snapshot_() const;
  static uint8_t condition_group_(uint8_t code);
  static std::string condition_text_(
      uint8_t code, uint8_t language);

  static constexpr uint32_t CITY_MAGIC = 0x57544831;
  static constexpr uint32_t CITY_PREF_KEY =
      fnv1a_hash("usp_weather_city_v1");
  static constexpr uint32_t MIN_RETRY_MS = 30000;
  static constexpr uint32_t MAX_RETRY_MS = 900000;

  mutable std::mutex state_mutex_;
  mutable std::mutex search_mutex_;
  mutable std::mutex city_mutex_;
  mutable std::mutex query_mutex_;

  WeatherSnapshot snapshot_{};
  std::array<SearchResult, SEARCH_RESULTS> search_results_{};
  StoredCity selected_city_{};
  std::string pending_query_;
  uint8_t pending_search_language_{2};
  // Generations bind each reply to the exact selection/query that requested it.
  uint64_t city_generation_{0};
  uint64_t query_generation_{0};
  uint64_t published_query_generation_{0};

  ESPPreferenceObject city_pref_{};
  TaskHandle_t worker_task_{nullptr};

  std::atomic<bool> stop_worker_{false};
  std::atomic<bool> refresh_requested_{false};
  std::atomic<bool> search_requested_{false};
  std::atomic<bool> update_pending_{false};
  std::atomic<bool> search_publish_pending_{false};

  uint32_t refresh_interval_ms_{900000};
  uint32_t stale_after_ms_{2700000};
  uint32_t initial_delay_ms_{12000};
  uint32_t next_refresh_ms_{0};
  uint32_t retry_delay_ms_{MIN_RETRY_MS};
  usp_weather_refresh::AttemptState forecast_attempt_state_{};
  usp_http_retry_after::Gate search_retry_gate_{};
  usp_http_retry_after::Gate forecast_retry_gate_{};
  bool last_stale_state_{true};

  Trigger<> update_trigger_;
  Trigger<> search_trigger_;
};

}  // namespace esphome::usp_weather
