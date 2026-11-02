#pragma once
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace usp_forecast_details {
template<size_t N> using Text = std::array<char, N>;
struct HourView {
  Text<24> feels{}, precip{};
  Text<64> wind{};
  Text<24> pressure{}, uv{};
};
struct DayView {
  Text<24> precip_sum{}, sunrise{}, sunset{}, uv_max{};
};
struct View { HourView hour{}; DayView day{}; };
static_assert(sizeof(View) <= 256, "WX5D detail view must remain bounded");

template<size_t N> void missing(Text<N> &out) { std::snprintf(out.data(), N, "--"); }
template<size_t N> void number(Text<N> &out, float value, const char *unit) {
  if (!std::isfinite(value)) { missing(out); return; }
  std::snprintf(out.data(), N, "%.1f%s", value, unit);
}
template<typename Hour> HourView hour(const Hour &row) {
  HourView out;
  number(out.feels, row.apparent_temperature_c, "°C");
  number(out.precip, row.precip_mm, " mm");
  if (std::isfinite(row.wind_kmh)) {
    if (std::isfinite(row.wind_direction_deg) && std::isfinite(row.gust_kmh))
      std::snprintf(out.wind.data(), out.wind.size(), "%.0f km/h · %.0f° · %.0f km/h", row.wind_kmh, row.wind_direction_deg, row.gust_kmh);
    else std::snprintf(out.wind.data(), out.wind.size(), "%.0f km/h", row.wind_kmh);
  } else missing(out.wind);
  number(out.pressure, row.pressure_hpa, " hPa");
  number(out.uv, row.uv_index, "");
  return out;
}
inline HourView empty_hour() {
  HourView out; missing(out.feels); missing(out.precip); missing(out.wind); missing(out.pressure); missing(out.uv); return out;
}
inline DayView empty_day() {
  DayView out; missing(out.precip_sum); missing(out.sunrise); missing(out.sunset); missing(out.uv_max); return out;
}
template<typename Day> DayView day(const Day &row) {
  DayView out;
  number(out.precip_sum, row.precip_sum_mm, " mm");
  if (row.sunrise[0]) std::snprintf(out.sunrise.data(), out.sunrise.size(), "%s", row.sunrise); else missing(out.sunrise);
  if (row.sunset[0]) std::snprintf(out.sunset.data(), out.sunset.size(), "%s", row.sunset); else missing(out.sunset);
  number(out.uv_max, row.uv_max, "");
  return out;
}
template<typename Snapshot> View build(const Snapshot &snapshot) {
  View out;
  out.hour = snapshot.valid && snapshot.hourly_count > 0 ? hour(snapshot.hours[0]) : empty_hour();
  out.day = snapshot.valid && snapshot.forecast_count > 0 ? day(snapshot.days[0]) : empty_day();
  return out;
}
}
