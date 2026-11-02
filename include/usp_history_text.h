#pragma once

namespace usp_history_text {

/** Keep named keys separate from widget ids and accepted caption spelling. */
enum class Label : unsigned {
  HISTORY_STATS = 0,
  POWER_AVAILABILITY = 1,
  MAINS_VS_SCHEDULE = 2,
  OUTAGE_EVENTS = 3,
  NO_HISTORY = 4,
  DATA_QUALITY = 5,
  WAITING = 6
};

inline constexpr const char *LABELS[3][7] = {
  {"ІСТОРІЯ / СТАТИСТИКА", "ДОСТУПНІСТЬ ЖИВЛЕННЯ", "Факт мережі проти графіка", "ПОДІЇ ВІДКЛЮЧЕНЬ", "Історії ще немає", "ЯКІСТЬ ДАНИХ", "ОЧІКУВАННЯ"},
  {"ИСТОРИЯ / СТАТИСТИКА", "ДОСТУПНОСТЬ ПИТАНИЯ", "Факт сети против графика", "СОБЫТИЯ ОТКЛЮЧЕНИЙ", "Истории пока нет", "КАЧЕСТВО ДАННЫХ", "ОЖИДАНИЕ"},
  {"HISTORY / STATISTICS", "POWER AVAILABILITY", "Match factual mains vs schedule", "OUTAGE EVENTS", "No history yet", "DATA QUALITY", "WAITING"}
};

/** Reference permanent text in the format expected by ESPHome text lambdas. */
struct TextRef {
  const char *value;
  constexpr const char *c_str() const { return value; }
};

/** Keep Ukrainian as the catalog fallback; existing language guards stay intact. */
inline constexpr unsigned language_index(unsigned language) {
  return language < 3 ? language : 0;
}

/** Return the established caption without allocation or widget side effects. */
inline constexpr TextRef label(unsigned language, Label key) {
  const unsigned index = static_cast<unsigned>(key);
  return {index < 7 ? LABELS[language_index(language)][index] : ""};
}

}  // namespace usp_history_text
