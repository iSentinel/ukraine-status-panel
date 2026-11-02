#pragma once

namespace usp_weather_page_text {
enum class Key : unsigned { WEATHER, NOW, DETAILS, REFRESH, BACK };
inline constexpr const char *TEXT[3][5] = {
    {"ПОГОДА", "ЗАРАЗ", "ДАНІ", "ОНОВИТИ", "НАЗАД"},
    {"ПОГОДА", "СЕЙЧАС", "ДАННЫЕ", "ОБНОВИТЬ", "НАЗАД"},
    {"WEATHER", "NOW", "DETAILS", "REFRESH", "BACK"},
};
inline constexpr const char *label(unsigned language, Key key) {
  const unsigned lang = language < 3 ? language : 2;
  const unsigned index = static_cast<unsigned>(key);
  return index < 5 ? TEXT[lang][index] : "";
}
}  // namespace usp_weather_page_text
