#pragma once

namespace usp_room_ui_text {

// Named labels keep the YAML bindings independent of table order.
enum class Label : unsigned {
  ROOM = 0,
  PERIOD = 1,
  INDOOR = 2,
  OUTDOOR = 3,
  HISTORY_24H = 4,
  WEATHER = 5,
  HOME = 6,
  SENSOR_ABSENT = 7,
  SENSOR_WAITING = 8,
  HISTORY_NOTE = 9,
  TEMP_24H = 10,
  HUMIDITY_24H = 11,
  NO_HISTORY = 12,
  LEFT_24H = 13,
  NOW = 14,
  DROPPED_PREFIX = 15,
};

inline constexpr const char *LABELS[16][3] = {
  {"В кімнаті", "В комнате", "Room"},
  {"24 год · крок 5 хв", "24 ч · шаг 5 мин", "24 h · 5 min"},
  {"В приміщенні", "В помещении", "Indoor"},
  {"Надворі", "На улице", "Outdoor"},
  {"Історія 24 год", "История 24 ч", "24h history"},
  {"ПОГОДА", "ПОГОДА", "WEATHER"},
  {"ГОЛОВНА", "ГЛАВНАЯ", "HOME"},
  {"SHT41/SHT45 · датчик не підключено", "SHT41/SHT45 · датчик не подключен", "SHT41/SHT45 · sensor not connected"},
  {"SHT41/SHT45 · очікування даних", "SHT41/SHT45 · ожидание данных", "SHT41/SHT45 · waiting for data"},
  {"Історія в RAM/PSRAM · після restart починається заново", "История в RAM/PSRAM · после restart начинается заново", "RAM/PSRAM history · restarts begin a new history"},
  {"ТЕМПЕРАТУРА · 24 ГОД", "ТЕМПЕРАТУРА · 24 Ч", "TEMPERATURE · 24H"},
  {"ВОЛОГІСТЬ · 24 ГОД", "ВЛАЖНОСТЬ · 24 Ч", "HUMIDITY · 24H"},
  {"Немає історії", "Нет истории", "No history"},
  {"-24 год", "-24 ч", "-24 h"},
  {"зараз", "сейчас", "now"},
  {"Останні сегменти; раніше пропущено", "Последние сегменты; ранее пропущено", "Latest segments; earlier runs omitted"},
};

// Preserve the existing picker fallback; callers clamp saved languages.
constexpr unsigned language_index(unsigned language) {
  return language < 2 ? language : 2;
}

// Invalid label values produce an empty caption without indexing the table.
constexpr const char *label(unsigned language, Label key) {
  const auto index = static_cast<unsigned>(key);
  return index < 16 ? LABELS[index][language_index(language)] : "";
}

}  // namespace usp_room_ui_text
