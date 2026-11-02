#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include "usp_ui_i18n_0_115_107.h"
#include "usp_weather_time.h"
#include "usp_weather_view_types.h"

namespace usp_weather_view {
/** Normalize an unsupported menu language to the existing English fallback. */
inline uint8_t language_index(uint8_t value) { return value < 3 ? value : 2; }

/** Keep missing and measured temperature text identical to the current screen. */
inline std::string temperature(float value, bool valid) {
  return valid ? usp_ui_i18n::temperature_c(static_cast<int>(std::lround(value)))
               : usp_ui_i18n::temperature_c_placeholder();
}

/** Format one available day or the current unavailable placeholders. */
template<typename Snapshot>
DayView day(const Snapshot &snapshot, size_t index, uint8_t language) {
  DayView view;
  language = language_index(language);
  const bool present = index < snapshot.forecast_count && index < snapshot.days.size();
  const char *prefix[] = {"Опади: ", "Осадки: ", "Precip: "};
  const std::string date = present ? snapshot.days[index].date : std::string{};
  view.date = date.size() >= 10 ? date.substr(8, 2) + "." + date.substr(5, 2) : "--.--";
  view.condition = usp_ui_i18n::compact_weather_condition(
      present ? std::string(usp_weather_codes::description(snapshot.days[index].code, language))
              : std::string{}, language);
  view.temperature = present ? usp_ui_i18n::temperature_c_range(
      static_cast<int>(std::lround(snapshot.days[index].high_c)),
      static_cast<int>(std::lround(snapshot.days[index].low_c)))
      : usp_ui_i18n::temperature_c_range_placeholder();
  view.rain = std::string(prefix[language]) + (present ?
      usp_ui::unsigned_decimal(snapshot.days[index].precip_percent) + "%" : "--%");
  return view;
}

/** Fill common current fields from one immutable snapshot and language. */
template<typename Snapshot>
void current(const Snapshot &snapshot, WeatherView &view) {
  const uint8_t language = view.language;
  const char *feels[] = {"Відчувається: ", "Ощущается: ", "Feels like: "};
  const char *humidity[] = {"Вологість: ", "Влажность: ", "Humidity: "};
  const char *wind[] = {"Вітер: ", "Ветер: ", "Wind: "};
  view.home_city = snapshot.city;
  view.city = snapshot.city + (snapshot.country.empty() ? "" : ", " + snapshot.country);
  view.temperature = temperature(snapshot.temperature_c, snapshot.valid);
  view.home_temperature = view.temperature;
  view.status = usp_ui_i18n::compact_weather_status(
      snapshot.valid, snapshot.stale_latched, snapshot.fetch_ok, language);
  view.status_color = snapshot.valid && !snapshot.stale_latched && snapshot.fetch_ok
      ? 0x4ADE80 : 0xFACC15;
  view.condition = snapshot.valid ? usp_ui_i18n::compact_weather_condition(
      usp_weather_codes::description(snapshot.weather_code, language), language) : "--";
  view.feels = std::string(feels[language]) +
      temperature(snapshot.apparent_temperature_c, snapshot.valid);
  view.humidity = std::string(humidity[language]) + (snapshot.valid ?
      usp_ui::unsigned_decimal(snapshot.humidity_percent) + "%" : "--%");
  view.wind = std::string(wind[language]) + usp_ui_i18n::wind_speed_kmh(
      snapshot.valid ? static_cast<int>(std::lround(snapshot.wind_kmh)) : 0,
      snapshot.valid, language);
  view.updated = usp_weather_time::as_of(snapshot.updated_time, language);
}

/** Capture one complete display transaction before yielded LVGL batches. */
template<typename Snapshot>
WeatherView build(const Snapshot &snapshot, uint8_t language) {
  WeatherView view;
  view.language = language_index(language);
  view.sequence = snapshot.sequence;
  current(snapshot, view);
  for (size_t index = 0; index < view.days.size(); ++index)
    view.days[index] = day(snapshot, index, view.language);
  return view;
}
}  // namespace usp_weather_view
