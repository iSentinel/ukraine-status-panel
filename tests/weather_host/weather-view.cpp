#include "usp_weather_view.h"
#include "weather-view-legacy.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
using Snapshot = esphome::usp_weather::WeatherClient::WeatherSnapshot;

/** Compare each dynamic label with the real original YAML lambda. */
void compare(const Snapshot &snapshot, uint8_t language) {
  const auto view = usp_weather_view::build(snapshot, language);
  assert(view.home_city == legacy_weather::home_weather_city_label(snapshot, language));
  assert(view.home_temperature == legacy_weather::home_weather_temp_label(snapshot, language));
  assert(view.city == legacy_weather::weather_city_label(snapshot, language));
  assert(view.status == legacy_weather::weather_status_label(snapshot, language));
  assert(view.temperature == legacy_weather::weather_current_temp_label(snapshot, language));
  assert(view.condition == legacy_weather::weather_condition_label(snapshot, language));
  assert(view.feels == legacy_weather::weather_feels_label(snapshot, language));
  assert(view.humidity == legacy_weather::weather_humidity_label(snapshot, language));
  assert(view.wind == legacy_weather::weather_wind_label(snapshot, language));
  assert(view.updated == legacy_weather::weather_updated_label(snapshot, language));
  assert(view.days[0].date == legacy_weather::weather_day0_date(snapshot, language));
  assert(view.days[0].condition == legacy_weather::weather_day0_condition(snapshot, language));
  assert(view.days[0].temperature == legacy_weather::weather_day0_temp(snapshot, language));
  assert(view.days[0].rain == legacy_weather::weather_day0_rain(snapshot, language));
  assert(view.days[1].date == legacy_weather::weather_day1_date(snapshot, language));
  assert(view.days[1].condition == legacy_weather::weather_day1_condition(snapshot, language));
  assert(view.days[1].temperature == legacy_weather::weather_day1_temp(snapshot, language));
  assert(view.days[1].rain == legacy_weather::weather_day1_rain(snapshot, language));
  assert(view.days[2].date == legacy_weather::weather_day2_date(snapshot, language));
  assert(view.days[2].condition == legacy_weather::weather_day2_condition(snapshot, language));
  assert(view.days[2].temperature == legacy_weather::weather_day2_temp(snapshot, language));
  assert(view.days[2].rain == legacy_weather::weather_day2_rain(snapshot, language));
  assert(view.days[3].date == legacy_weather::weather_day3_date(snapshot, language));
  assert(view.days[3].condition == legacy_weather::weather_day3_condition(snapshot, language));
  assert(view.days[3].temperature == legacy_weather::weather_day3_temp(snapshot, language));
  assert(view.days[3].rain == legacy_weather::weather_day3_rain(snapshot, language));
  const uint32_t color = snapshot.valid && !snapshot.stale_latched && snapshot.fetch_ok
      ? 0x4ADE80 : 0xFACC15;
  assert(view.status_color == color);
}

/** Ensure a rendered transaction survives later provider and locale changes. */
void owning_view() {
  Snapshot snapshot;
  snapshot.city = "Київ";
  snapshot.country = "Україна";
  snapshot.valid = snapshot.fetch_ok = true;
  snapshot.sequence = 42;
  snapshot.forecast_count = 1;
  std::strcpy(snapshot.days[0].date, "2026-10-25");
  const auto view = usp_weather_view::build(snapshot, 0);
  snapshot.city = "Changed"; snapshot.country.clear();
  snapshot.sequence = 43; snapshot.forecast_count = 0;
  snapshot.days[0].date[0] = '\0';
  assert(view.home_city == "Київ" && view.city == "Київ, Україна");
  assert(view.sequence == 42 && view.language == 0);
  assert(view.days[0].date == "25.10");
  assert(usp_weather_view::build(snapshot, 255).language == 2);
}

/** Exercise every code, locale, quality state and supported daily count. */
int main() {
  owning_view();
  Snapshot snapshot;
  snapshot.city = "Київ · Київська область";
  snapshot.updated_time = "2026-10-25T03:15";
  snapshot.temperature_c = -3.5F;
  snapshot.apparent_temperature_c = -5.5F;
  snapshot.humidity_percent = 94;
  snapshot.wind_kmh = 12.5F;
  for (unsigned code = 0; code < 256; ++code) {
    snapshot.weather_code = code;
    for (size_t i = 0; i < snapshot.days.size(); ++i) {
      auto &day = snapshot.days[i];
      std::snprintf(day.date, sizeof(day.date), "2026-10-%02u", unsigned(23+i));
      day.code = (code+i) % 256; day.high_c = 7.5F+i; day.low_c = -2.5F+i;
      day.precip_percent = i*15;
    }
    for (unsigned quality = 0; quality < 8; ++quality) {
      snapshot.valid = quality & 1; snapshot.fetch_ok = quality & 2;
      snapshot.stale_latched = quality & 4;
      snapshot.country = quality & 1 ? "Україна" : "";
      for (size_t count = 0; count <= 7; ++count) {
        snapshot.forecast_count = count;
        for (uint8_t language = 0; language < 3; ++language) compare(snapshot, language);
      }
    }
  }
  std::cout << "WEATHER_VIEW_EQUIVALENCE=PASS cases=49152 dynamic_labels=26 WMO256 languages3 quality8 forecast_counts8\n";
}
