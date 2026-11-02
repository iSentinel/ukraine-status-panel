#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include "usp_provider_time.h"
#include "usp_weather_view.h"
#include "usp_forecast_view_types.h"

namespace usp_forecast_view {
/** Add weekday text derived from the provider date, independent of host TZ. */
inline std::string weekday(std::string_view date, uint8_t language) {
  static constexpr const char *NAMES[3][7] = {
    {"Пн","Вт","Ср","Чт","Пт","Сб","Нд"},
    {"Пн","Вт","Ср","Чт","Пт","Сб","Вс"},
    {"Mon","Tue","Wed","Thu","Fri","Sat","Sun"}};
  const int64_t day = usp_provider_time::calendar_day(date);
  return day < 0 ? "--" : NAMES[usp_weather_view::language_index(language)][day % 7];
}

/** Anchor hours at the next provider-local whole hour, using its fixed offset. */
template<typename Snapshot>
std::optional<int64_t> first_hour(const Snapshot &snapshot) {
  const auto epoch = usp_provider_time::utc_epoch(
      snapshot.updated_time, snapshot.utc_offset_seconds, snapshot.time_context_valid);
  if (!epoch) return {};
  const int64_t wall_minute = usp_provider_time::minute(snapshot.updated_time);
  const int second = snapshot.updated_time.size() == 19
      ? usp_provider_time::decimal(snapshot.updated_time, 17, 2) : 0;
  const int64_t in_hour = (wall_minute % 60) * 60 + second;
  return *epoch + (3600 - in_hour) % 3600;
}

/** Reject non-finite or non-displayable temperatures before integer formatting. */
inline bool display_temperature(float value) {
  return std::isfinite(value) && value >= -200.0F && value <= 200.0F;
}

/** Preserve daily order and missing values; never manufacture extra dates. */
template<typename Snapshot>
void fill_days(const Snapshot &snapshot, View &view) {
  for (size_t index = 0; index < view.days.size(); ++index) {
    const bool present = snapshot.valid && index < snapshot.forecast_count &&
        index < snapshot.days.size() &&
        usp_weather_time::valid_date(snapshot.days[index].date) &&
        display_temperature(snapshot.days[index].high_c) &&
        display_temperature(snapshot.days[index].low_c) &&
        snapshot.days[index].high_c >= snapshot.days[index].low_c;
    if (present) {
      view.days[index] = usp_weather_view::day(snapshot, index, view.language);
      view.days[index].date = weekday(snapshot.days[index].date, view.language) +
          " · " + view.days[index].date;
      view.day_icons[index] = usp_weather_icon_model::style(
          snapshot.days[index].code,
          static_cast<int>(std::lround(snapshot.days[index].high_c)), true);
    } else {
      view.days[index] = {"-- · --.--", "--", "-- / --°C", "--%"};
    }
  }
}

/** Map real rows onto fixed hourly slots; omitted rows leave explicit gaps. */
template<typename Snapshot>
void fill_points(const Snapshot &snapshot, View &view, int64_t first) {
  for (size_t i = 0; i < view.points.size(); ++i) view.points[i].utc = first + i * 3600;
  for (size_t row = 0; row < snapshot.hourly_count && row < snapshot.hours.size(); ++row) {
    const auto &hour = snapshot.hours[row];
    const auto epoch = usp_provider_time::utc_epoch(
        hour.time, snapshot.utc_offset_seconds, snapshot.time_context_valid);
    if (!epoch || *epoch < first || *epoch - first >= 24 * 3600 ||
        (*epoch - first) % 3600 != 0 || !display_temperature(hour.temperature_c)) continue;
    auto &point = view.points[static_cast<size_t>((*epoch - first) / 3600)];
    if (point.available) continue;
    point.temperature = hour.temperature_c;
    point.code = hour.code;
    point.source_index = static_cast<uint8_t>(row);
    point.available = true;
  }
}

/** Bound a usable chart scale, including flat and entirely missing series. */
inline void chart_range(View &view) {
  float low = std::numeric_limits<float>::max();
  float high = std::numeric_limits<float>::lowest();
  bool any = false;
  for (size_t i = 0; i < view.points.size(); ++i) {
    auto &point = view.points[i];
    point.break_before = i == 0 || !view.points[i-1].available || !point.available;
    if (!point.available) continue;
    any = true;
    low = std::min(low, point.temperature);
    high = std::max(high, point.temperature);
  }
  if (any) {
    view.minimum = std::floor(low) - 1;
    view.maximum = std::ceil(high) + 1;
  }
}

/** Select six fixed grid slots instead of packing missing observations together. */
template<typename Snapshot>
void fill_hours(const Snapshot &snapshot, View &view) {
  const char *rain[] = {"Опади: ", "Осадки: ", "Precip: "};
  for (size_t i = 0; i < view.hours.size(); ++i) {
    auto &card = view.hours[i];
    const size_t slot = view.window * 6 + i;
    const auto &point = view.points[slot];
    if (!point.available) continue;
    const auto &hour = snapshot.hours[point.source_index];
    card.time = std::string(hour.time).substr(11, 5);
    card.temperature = usp_weather_view::temperature(point.temperature, true);
    card.condition = usp_ui_i18n::compact_weather_condition(
        usp_weather_codes::description(point.code, view.language), view.language);
    card.rain = std::string(rain[view.language]) +
        (hour.precip_percent <= 100 ? usp_ui::unsigned_decimal(hour.precip_percent) + "%" : "--%");
    card.icon = usp_weather_icon_model::style(
        point.code, static_cast<int>(std::lround(point.temperature)), hour.is_day);
    card.available = true;
  }
}

/** Build pure owning output; process timezone, UI state and clocks are unused. */
template<typename Snapshot>
View build(const Snapshot &snapshot, uint8_t language, uint8_t window) {
  View view;
  view.sequence = snapshot.sequence;
  view.city = snapshot.city;
  view.language = usp_weather_view::language_index(language);
  view.window = window < 4 ? window : 0;
  view.stale = snapshot.stale_latched;
  view.fetch_ok = snapshot.fetch_ok;
  view.updated = usp_weather_time::as_of(snapshot.updated_time, view.language);
  view.status = usp_ui_i18n::compact_weather_status(
      snapshot.valid, snapshot.stale_latched, snapshot.fetch_ok, view.language);
  view.time_context = usp_weather_timezone::offset_label(
      snapshot.utc_offset_seconds, snapshot.time_context_valid);
  fill_days(snapshot, view);
  const auto first = first_hour(snapshot);
  view.time_valid = snapshot.valid && first.has_value();
  if (view.time_valid) {
    fill_points(snapshot, view, *first);
    chart_range(view);
    fill_hours(snapshot, view);
  }
  return view;
}
}  // namespace usp_forecast_view
