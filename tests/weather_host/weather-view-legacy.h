#pragma once
#include "weather_client.h"
#include "usp_ui_i18n_0_115_107.h"
#include "usp_weather_time.h"
#include <cmath>
namespace legacy_weather {
template<typename Snapshot> std::string home_weather_city_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
return snapshot.city;
}
template<typename Snapshot> std::string home_weather_temp_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
if (!snapshot.valid) {
  return usp_ui_i18n::temperature_c_placeholder();
}
const int value = static_cast<int>(
    std::lround(snapshot.temperature_c));
return usp_ui_i18n::temperature_c(value);
}
template<typename Snapshot> std::string weather_city_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
std::string text = snapshot.city;
const std::string country = snapshot.country;
if (!country.empty()) {
  text += ", " + country;
}
return text;
}
template<typename Snapshot> std::string weather_status_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
return usp_ui_i18n::compact_weather_status(
    snapshot.valid,
    snapshot.stale_latched,
    snapshot.fetch_ok, language);
}
template<typename Snapshot> std::string weather_current_temp_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
if (!snapshot.valid) {
  return usp_ui_i18n::temperature_c_placeholder();
}
const int value = static_cast<int>(
    std::lround(snapshot.temperature_c));
return usp_ui_i18n::temperature_c(value);
}
template<typename Snapshot> std::string weather_condition_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
if (!snapshot.valid) {
  return std::string("--");
}
return usp_ui_i18n::compact_weather_condition(
    esphome::usp_weather::WeatherClient::condition_text(snapshot.weather_code, language),
    language);
}
template<typename Snapshot> std::string weather_feels_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
std::string prefix;
if (language == 0) prefix = "Відчувається: ";
else if (language == 1) prefix = "Ощущается: ";
else prefix = "Feels like: ";
if (!snapshot.valid) {
  return prefix + usp_ui_i18n::temperature_c_placeholder();
}
const int value = static_cast<int>(std::lround(
    snapshot.apparent_temperature_c));
return prefix + usp_ui_i18n::temperature_c(value);
}
template<typename Snapshot> std::string weather_humidity_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
std::string prefix;
if (language == 0) prefix = "Вологість: ";
else if (language == 1) prefix = "Влажность: ";
else prefix = "Humidity: ";
if (!snapshot.valid) return prefix + "--%";
return prefix + usp_ui::unsigned_decimal(
    snapshot.humidity_percent) + "%";
}
template<typename Snapshot> std::string weather_wind_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
std::string prefix;
if (language == 0) prefix = "Вітер: ";
else if (language == 1) prefix = "Ветер: ";
else prefix = "Wind: ";
if (!snapshot.valid) {
  return prefix + usp_ui_i18n::wind_speed_kmh(
      0, false, language);
}
const int value = static_cast<int>(
    std::lround(snapshot.wind_kmh));
return prefix + usp_ui_i18n::wind_speed_kmh(
    value, true, language);
}
template<typename Snapshot> std::string weather_updated_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
return usp_weather_time::as_of(
    snapshot.updated_time, language);
}
template<typename Snapshot> std::string weather_title_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
if (language == 0) return std::string("ПОГОДА");
if (language == 1) return std::string("ПОГОДА");
return std::string("WEATHER");
}
template<typename Snapshot> std::string weather_now_title_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
if (language == 0) return std::string("ЗАРАЗ");
if (language == 1) return std::string("СЕЙЧАС");
return std::string("NOW");
}
template<typename Snapshot> std::string weather_details_title_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
if (language == 0) return std::string("ДАНІ");
if (language == 1) return std::string("ДАННЫЕ");
return std::string("DETAILS");
}
template<typename Snapshot> std::string weather_provider_note_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
return usp_ui_i18n::weather_data_attribution(
    language, " · ");
}
template<typename Snapshot> std::string weather_footer_attribution_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
return usp_ui_i18n::weather_open_meteo_attribution(
    language);
}
template<typename Snapshot> std::string weather_full_attribution_label(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
return usp_ui_i18n::weather_data_attribution(
    language, " / ");
}
template<typename Snapshot> std::string weather_day0_date(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
const std::string date = (snapshot.forecast_count > 0 ? snapshot.days[0].date : std::string{});
if (date.size() >= 10) {
  return date.substr(8, 2) + "." + date.substr(5, 2);
}
return std::string("--.--");
}
template<typename Snapshot> std::string weather_day0_condition(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
return usp_ui_i18n::compact_weather_condition(
    (snapshot.forecast_count > 
        0 ? esphome::usp_weather::WeatherClient::condition_text(snapshot.days[
        0].code, language) : std::string{}), language);
}
template<typename Snapshot> std::string weather_day0_temp(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
if (snapshot.forecast_count <= 0) {
  return usp_ui_i18n::temperature_c_range_placeholder();
}
const int high = static_cast<int>(std::lround(
    (snapshot.forecast_count > 0 ? snapshot.days[0].high_c : 0)));
const int low = static_cast<int>(std::lround(
    (snapshot.forecast_count > 0 ? snapshot.days[0].low_c : 0)));
return usp_ui_i18n::temperature_c_range(high, low);
}
template<typename Snapshot> std::string weather_day0_rain(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
std::string prefix;
if (language == 0) prefix = "Опади: ";
else if (language == 1) prefix = "Осадки: ";
else prefix = "Precip: ";
if (snapshot.forecast_count <= 0) {
  return prefix + "--%";
}
return prefix + usp_ui::unsigned_decimal(
    (snapshot.forecast_count > 0 ? snapshot.days[0].precip_percent : 0)) + "%";
}
template<typename Snapshot> std::string weather_day1_date(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
const std::string date = (snapshot.forecast_count > 1 ? snapshot.days[1].date : std::string{});
if (date.size() >= 10) {
  return date.substr(8, 2) + "." + date.substr(5, 2);
}
return std::string("--.--");
}
template<typename Snapshot> std::string weather_day1_condition(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
return usp_ui_i18n::compact_weather_condition(
    (snapshot.forecast_count > 
        1 ? esphome::usp_weather::WeatherClient::condition_text(snapshot.days[
        1].code, language) : std::string{}), language);
}
template<typename Snapshot> std::string weather_day1_temp(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
if (snapshot.forecast_count <= 1) {
  return usp_ui_i18n::temperature_c_range_placeholder();
}
const int high = static_cast<int>(std::lround(
    (snapshot.forecast_count > 1 ? snapshot.days[1].high_c : 0)));
const int low = static_cast<int>(std::lround(
    (snapshot.forecast_count > 1 ? snapshot.days[1].low_c : 0)));
return usp_ui_i18n::temperature_c_range(high, low);
}
template<typename Snapshot> std::string weather_day1_rain(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
std::string prefix;
if (language == 0) prefix = "Опади: ";
else if (language == 1) prefix = "Осадки: ";
else prefix = "Precip: ";
if (snapshot.forecast_count <= 1) {
  return prefix + "--%";
}
return prefix + usp_ui::unsigned_decimal(
    (snapshot.forecast_count > 1 ? snapshot.days[1].precip_percent : 0)) + "%";
}
template<typename Snapshot> std::string weather_day2_date(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
const std::string date = (snapshot.forecast_count > 2 ? snapshot.days[2].date : std::string{});
if (date.size() >= 10) {
  return date.substr(8, 2) + "." + date.substr(5, 2);
}
return std::string("--.--");
}
template<typename Snapshot> std::string weather_day2_condition(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
return usp_ui_i18n::compact_weather_condition(
    (snapshot.forecast_count > 
        2 ? esphome::usp_weather::WeatherClient::condition_text(snapshot.days[
        2].code, language) : std::string{}), language);
}
template<typename Snapshot> std::string weather_day2_temp(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
if (snapshot.forecast_count <= 2) {
  return usp_ui_i18n::temperature_c_range_placeholder();
}
const int high = static_cast<int>(std::lround(
    (snapshot.forecast_count > 2 ? snapshot.days[2].high_c : 0)));
const int low = static_cast<int>(std::lround(
    (snapshot.forecast_count > 2 ? snapshot.days[2].low_c : 0)));
return usp_ui_i18n::temperature_c_range(high, low);
}
template<typename Snapshot> std::string weather_day2_rain(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
std::string prefix;
if (language == 0) prefix = "Опади: ";
else if (language == 1) prefix = "Осадки: ";
else prefix = "Precip: ";
if (snapshot.forecast_count <= 2) {
  return prefix + "--%";
}
return prefix + usp_ui::unsigned_decimal(
    (snapshot.forecast_count > 2 ? snapshot.days[2].precip_percent : 0)) + "%";
}
template<typename Snapshot> std::string weather_day3_date(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
const std::string date = (snapshot.forecast_count > 3 ? snapshot.days[3].date : std::string{});
if (date.size() >= 10) {
  return date.substr(8, 2) + "." + date.substr(5, 2);
}
return std::string("--.--");
}
template<typename Snapshot> std::string weather_day3_condition(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
return usp_ui_i18n::compact_weather_condition(
    (snapshot.forecast_count > 
        3 ? esphome::usp_weather::WeatherClient::condition_text(snapshot.days[
        3].code, language) : std::string{}), language);
}
template<typename Snapshot> std::string weather_day3_temp(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
if (snapshot.forecast_count <= 3) {
  return usp_ui_i18n::temperature_c_range_placeholder();
}
const int high = static_cast<int>(std::lround(
    (snapshot.forecast_count > 3 ? snapshot.days[3].high_c : 0)));
const int low = static_cast<int>(std::lround(
    (snapshot.forecast_count > 3 ? snapshot.days[3].low_c : 0)));
return usp_ui_i18n::temperature_c_range(high, low);
}
template<typename Snapshot> std::string weather_day3_rain(const Snapshot &snapshot, [[maybe_unused]] uint8_t language) {
std::string prefix;
if (language == 0) prefix = "Опади: ";
else if (language == 1) prefix = "Осадки: ";
else prefix = "Precip: ";
if (snapshot.forecast_count <= 3) {
  return prefix + "--%";
}
return prefix + usp_ui::unsigned_decimal(
    (snapshot.forecast_count > 3 ? snapshot.days[3].precip_percent : 0)) + "%";
}
}
