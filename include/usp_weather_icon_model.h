#pragma once
#include <cstdint>
#include "usp_weather_codes.h"

namespace usp_weather_icon_model {
enum class Shape : uint8_t { SUN, CRESCENT, CLOUD, SNOW, THUNDER, UNKNOWN };
struct Style {
  Shape shape{Shape::UNKNOWN};
  uint32_t primary{0x94A3B8U};
  uint32_t accent{0x94A3B8U};
  bool rain_drops{false};
};

constexpr const char *glyph(Shape shape) {
  switch (shape) {
    case Shape::SUN: return "☀";
    case Shape::CLOUD: return "☁";
    case Shape::SNOW: return "❄";
    case Shape::THUNDER: return "⚡";
    default: return "";
  }
}

constexpr Style style(uint8_t code, int temperature_c, bool is_day) {
  using namespace usp_weather_codes;
  if (code == 0) {
    if (!is_day) return {Shape::CRESCENT, 0x93C5FDU, 0x93C5FDU, false};
    return {Shape::SUN, temperature_c >= 30 ? 0xF97316U : 0xFACC15U,
            temperature_c >= 30 ? 0xF97316U : 0xFACC15U, false};
  }
  if (code == 1 || code == 2)
    return {Shape::CLOUD, 0x60A5FAU, 0x60A5FAU, false};
  if (code == 3 || code == 45 || code == 48)
    return {Shape::CLOUD, 0x94A3B8U, 0x94A3B8U, false};
  if (has_rain(code))
    return {Shape::CLOUD, 0x94A3B8U, 0x38BDF8U, true};
  if (has_snow(code))
    return {Shape::SNOW, 0xF8FAFCU, 0xF8FAFCU, false};
  if (group(code) == THUNDERSTORM)
    return {Shape::THUNDER, 0xFBBF24U, 0xFBBF24U, false};
  return {};
}
}  // namespace usp_weather_icon_model
