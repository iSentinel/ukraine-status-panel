#pragma once

#include "usp_weather_view_types.h"
#include "usp_weather_icon_model.h"

#include <array>
#include <cstdint>
#include <string>

namespace usp_forecast_view {

struct Point {
  int64_t utc{0};
  float temperature{0.0F};
  uint8_t code{255};
  uint8_t source_index{255};
  bool available{false};
  bool break_before{true};
};

struct HourView {
  std::string time{"--:--"};
  std::string temperature{"--°C"};
  std::string condition{"--"};
  std::string rain{"--%"};
  usp_weather_icon_model::Style icon{};
  bool available{false};
};

struct View {
  std::array<usp_weather_view::DayView, 7> days{};
  std::array<Point, 24> points{};
  std::array<HourView, 6> hours{};
  std::array<usp_weather_icon_model::Style, 7> day_icons{};
  std::string city;
  std::string time_context;
  std::string updated;
  std::string status;
  float minimum{-1.0F};
  float maximum{1.0F};
  uint32_t sequence{0};
  uint8_t language{2};
  uint8_t window{0};
  bool time_valid{false};
  bool stale{false};
  bool fetch_ok{false};
};

static_assert(sizeof(View) <= 3072, "Keep forecast view storage bounded");

}  // namespace usp_forecast_view
