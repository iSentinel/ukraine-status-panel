#pragma once

namespace usp_home_page_text {
enum class Label : unsigned {
  PAGE_TITLE = 0,
  ALERT_REGION = 1,
  KYIV = 2,
  MAP = 3,
  POWER_HEALTH = 4,
  MAINS_SCHEDULE = 5,
  INTERNET = 6,
  CONNECTED = 7,
  PROVIDER_HEALTH = 8,
  OUTAGES = 9,
  NO_DATA = 10,
  SCHEDULE_FACT = 11,
  WEATHER = 12,
  FOUR_DAYS = 13,
  MAP_TITLE = 14,
  MAP_EXPAND = 15,
  ALERTS_TITLE = 16,
  POWER_TITLE = 17,
  HISTORY_TITLE = 18,
  OUTAGE_TITLE = 19,
  MAINS_CAPTION = 20,
  BATTERY_CAPTION = 21,
  WIFI_CAPTION = 22,
  YASNO_GROUP = 23,
  MAINS_HEADER = 24
};
inline constexpr const char *LABELS[3][25] = {
  {"СТАТУС", "ТРИВОГИ / РЕГІОН", "КИЇВ", "МАПА", "СТАН ЖИВЛЕННЯ", "Мережа / графік", "ІНТЕРНЕТ", "ПІДКЛЮЧЕНО", "Стан провайдера", "ВІДКЛЮЧЕННЯ", "НЕМАЄ ДАНИХ", "Графік / факт", "ПОГОДА", "4 ДНІ", "Мапа України", "ПОВНА", "Активні тривоги", "Живлення · Shelly", "Погода · Кімната", "Графік відключень", "Живлення від мережі", "Акумулятор", "Wi-Fi", "Група Yasno", "Мережа"},
  {"СТАТУС", "ТРЕВОГИ / РЕГИОН", "КИЕВ", "КАРТА", "СОСТОЯНИЕ ПИТАНИЯ", "Сеть / график", "ИНТЕРНЕТ", "ПОДКЛЮЧЕНО", "Состояние провайдера", "ОТКЛЮЧЕНИЯ", "НЕТ ДАННЫХ", "График / факт", "ПОГОДА", "4 ДНЯ", "Карта Украины", "ПОЛНАЯ", "Активные тревоги", "Питание · Shelly", "Погода · Комната", "График отключений", "Питание от сети", "Батарея", "Wi-Fi", "Группа Yasno", "Сеть"},
  {"STATUS", "ALERTS / REGION", "KYIV", "MAP", "POWER HEALTH", "Mains / schedule", "INTERNET", "CONNECTED", "Provider health", "OUTAGES", "NO DATA", "Schedule / factual", "WEATHER", "4 DAYS", "Map of Ukraine", "FULL", "Active Alerts", "Power · Shelly", "Weather · Room", "Outage Schedule", "Mains power", "Battery", "Wi-Fi", "Yasno group", "Mains"}
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
  return {index < 25 ? LABELS[language_index(language)][index] : ""};
}
}  // namespace usp_home_page_text
