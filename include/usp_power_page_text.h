#pragma once

namespace usp_power_page_text {
enum class Label : unsigned {
  PAGE_TITLE = 0,
  FACTUAL_MAINS = 1,
  WAITING = 2,
  MAINS_SENSOR = 3,
  SCHEDULE = 4,
  NO_DATA = 5,
  RANGE_HINT = 6,
  POWER_TIMELINE = 7
};
inline constexpr const char *LABELS[3][8] = {
  {"СТАН ЖИВЛЕННЯ", "ФАКТИЧНА МЕРЕЖА", "ОЧІКУВАННЯ", "Фізичний датчик мережі", "ГРАФІК", "НЕМАЄ ДАНИХ", "Сьогодні / Завтра / Тиждень", "ШКАЛА ЖИВЛЕННЯ"},
  {"СОСТОЯНИЕ ПИТАНИЯ", "ФАКТИЧЕСКАЯ СЕТЬ", "ОЖИДАНИЕ", "Физический датчик сети", "ГРАФИК", "НЕТ ДАННЫХ", "Сегодня / Завтра / Неделя", "ШКАЛА ПИТАНИЯ"},
  {"POWER HEALTH", "FACTUAL MAINS", "WAITING", "Physical mains sensor", "SCHEDULE", "NO DATA", "Today / Tomorrow / Week", "POWER TIMELINE"}
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
  return {index < 8 ? LABELS[language_index(language)][index] : ""};
}
}  // namespace usp_power_page_text
