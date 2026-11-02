#pragma once

namespace usp_weather_fullscreen_text {

enum class Label : unsigned {
  HOURLY = 0,
  DAILY = 1,
  DETAILS = 2,
};

inline constexpr const char *LABELS[3][3] = {
  {"ПОГОДИННО", "ПО ЧАСАМ", "HOURLY"},
  {"7 ДНІВ", "7 ДНЕЙ", "7 DAYS"},
  {"ДЕТАЛІ", "ДЕТАЛИ", "DETAILS"},
};

// Preserve current refresh_weather_fullscreen_ui fallback: invalid language -> UA.
constexpr const char *label(unsigned language, Label key) {
  const auto index = static_cast<unsigned>(key);
  const auto lang = language < 3 ? language : 0;
  return index < 3 ? LABELS[index][lang] : "";
}

}  // namespace usp_weather_fullscreen_text
