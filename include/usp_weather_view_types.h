#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace usp_weather_view {
/** Own the four texts used by one existing forecast card. */
struct DayView {
  std::string date, condition, temperature, rain;
};
/** Own shared text without referring to LVGL widgets or provider buffers. */
struct WeatherView {
  std::string home_city, home_temperature, city, status, temperature;
  std::string condition, feels, humidity, wind, updated;
  std::array<DayView, 4> days{};
  uint32_t status_color{0xFACC15};
  uint32_t sequence{0};
  uint8_t language{2};
};

static_assert(sizeof(WeatherView) <= 1024, "Keep owning weather text bounded");

}  // namespace usp_weather_view
