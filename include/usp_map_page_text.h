#pragma once

namespace usp_map_page_text {
enum class Label : unsigned {
  PAGE_TITLE = 0,
  COUNTRY = 1,
  REGIONAL_MAP = 2,
  LAYER_PLACEHOLDER = 3,
  SELECTED_REGION = 4,
  KYIV = 5,
  THREAT = 6,
  TEMPERATURE = 7,
  RADIATION = 8,
  GRID = 9
};
inline constexpr const char *LABELS[3][10] = {
  {"МАПА ТРИВОГ", "УКРАЇНА", "МАПА РЕГІОНІВ", "Дані буде підключено", "ОБРАНИЙ РЕГІОН", "КИЇВ", "Загроза", "Температура", "Радіація", "Енергомережа"},
  {"КАРТА ТРЕВОГ", "УКРАИНА", "КАРТА РЕГИОНОВ", "Данные будут подключены", "ВЫБРАННЫЙ РЕГИОН", "КИЕВ", "Угроза", "Температура", "Радиация", "Энергосеть"},
  {"ALERT MAP", "UKRAINE", "REGIONAL MAP", "Data layer will be connected", "SELECTED REGION", "KYIV", "Threat", "Temperature", "Radiation", "Grid"}
};
struct TextRef {
  const char *value;
  constexpr const char *c_str() const { return value; }
};
// Unknown languages use the existing Ukrainian fallback.
constexpr unsigned language_index(unsigned language) {
  return language < 3 ? language : 0;
}
constexpr TextRef label(unsigned language, Label key) {
  const auto index = static_cast<unsigned>(key);
  return {index < 10 ? LABELS[language_index(language)][index] : ""};
}
}  // namespace usp_map_page_text
