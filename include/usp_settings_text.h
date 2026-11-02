#pragma once

namespace usp_settings_text {

/** Keep named keys separate from widget ids and accepted caption spelling. */
enum class Label : unsigned {
  PAGE_TITLE = 0,
  DISPLAY_SECTION = 1,
  BRIGHTNESS = 2,
  BATTERY_POLICY = 3,
  HOME_REGION = 4,
  LANGUAGE = 5,
  LANGUAGE_NAME = 6,
  MAP_COLORS = 7,
  SYSTEM_SECTION = 8,
  FIRMWARE = 9,
  UPDATE_POLICY = 10,
  MANUAL_UPDATE = 11,
  DIAGNOSTICS = 12,
  DISPLAY_OFF = 13,
  RESTART = 14
};

inline constexpr const char *LABELS[3][15] = {
  {"НАЛАШТУВАННЯ", "ЕКРАН", "Яскравість", "Політика батареї", "Домашній регіон", "Мова", "Українська", "Кольори карти", "СИСТЕМА", "Прошивка", "Політика оновлень", "ВРУЧНУ", "ДІАГНОСТИКА", "ВИМК. ЕКРАН", "ПЕРЕЗАПУСК"},
  {"НАСТРОЙКИ", "ЭКРАН", "Яркость", "Политика батареи", "Домашний регион", "Язык", "Русский", "Цвета карты", "СИСТЕМА", "Прошивка", "Политика обновлений", "ВРУЧНУЮ", "ДИАГНОСТИКА", "ВЫКЛ. ЭКРАН", "ПЕРЕЗАПУСК"},
  {"SETTINGS", "DISPLAY", "Brightness", "Battery policy", "Home region", "Language", "English", "Map colors", "SYSTEM", "Firmware", "Update policy", "MANUAL", "DIAGNOSTICS", "SCREEN OFF", "RESTART"}
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
  return {index < 15 ? LABELS[language_index(language)][index] : ""};
}

}  // namespace usp_settings_text
