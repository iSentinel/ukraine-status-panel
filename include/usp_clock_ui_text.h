#pragma once

namespace usp_clock_ui_text {

// Stable names are verified independently of table positions.
enum class Label : unsigned {
  CLOCK = 0,
  OUTDOOR_WEATHER = 1,
  CLEAR = 2,
  ALERT = 3,
  REGION_STATUS = 4,
  MAP_UNAVAILABLE = 5,
};

inline constexpr const char *LABELS[6][3] = {
  {"Годинник", "Часы", "Clock"},
  {"Погода надворі", "Погода на улице", "Outdoor weather"},
  {"Відбій", "Отбой", "Clear"},
  {"Тривога", "Тревога", "Alert"},
  {"Статус регіону", "Статус региона", "Region status"},
  {"Карта недоступна", "Карта недоступна", "Map unavailable"},
};

// Preserve the picker English fallback; the quality-state table is separate.
constexpr const char *label(unsigned language, Label key) {
  const auto index = static_cast<unsigned>(key);
  const auto lang = language < 2 ? language : 2;
  return index < 6 ? LABELS[index][lang] : "";
}

}  // namespace usp_clock_ui_text
