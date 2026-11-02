#include <cassert>
#include <cstring>
#include <iostream>
#include "usp_settings_text.h"
static const char *golden[3][15] = {
  {"НАЛАШТУВАННЯ", "ЕКРАН", "Яскравість", "Політика батареї", "Домашній регіон", "Мова", "Українська", "Кольори карти", "СИСТЕМА", "Прошивка", "Політика оновлень", "ВРУЧНУ", "ДІАГНОСТИКА", "ВИМК. ЕКРАН", "ПЕРЕЗАПУСК"},
  {"НАСТРОЙКИ", "ЭКРАН", "Яркость", "Политика батареи", "Домашний регион", "Язык", "Русский", "Цвета карты", "СИСТЕМА", "Прошивка", "Политика обновлений", "ВРУЧНУЮ", "ДИАГНОСТИКА", "ВЫКЛ. ЭКРАН", "ПЕРЕЗАПУСК"},
  {"SETTINGS", "DISPLAY", "Brightness", "Battery policy", "Home region", "Language", "English", "Map colors", "SYSTEM", "Firmware", "Update policy", "MANUAL", "DIAGNOSTICS", "SCREEN OFF", "RESTART"}
};
int main() {
  const usp_settings_text::Label keys[] = {usp_settings_text::Label::PAGE_TITLE, usp_settings_text::Label::DISPLAY_SECTION, usp_settings_text::Label::BRIGHTNESS, usp_settings_text::Label::BATTERY_POLICY, usp_settings_text::Label::HOME_REGION, usp_settings_text::Label::LANGUAGE, usp_settings_text::Label::LANGUAGE_NAME, usp_settings_text::Label::MAP_COLORS, usp_settings_text::Label::SYSTEM_SECTION, usp_settings_text::Label::FIRMWARE, usp_settings_text::Label::UPDATE_POLICY, usp_settings_text::Label::MANUAL_UPDATE, usp_settings_text::Label::DIAGNOSTICS, usp_settings_text::Label::DISPLAY_OFF, usp_settings_text::Label::RESTART};
  unsigned checked = 0;
  for (unsigned language = 0; language < 256; ++language) {
    const unsigned old = language < 3 ? language : 0;
    for (unsigned index = 0; index < 15; ++index) {
      assert(std::strcmp(golden[old][index], usp_settings_text::label(language, keys[index]).c_str()) == 0);
      ++checked;
    }
  }
  assert(std::strcmp("", usp_settings_text::label(0, static_cast<usp_settings_text::Label>(99)).c_str()) == 0);
  assert(checked == 3840);
  std::cout << "SETTINGS_TEXT_CATALOG_NATIVE=PASS checked=" << checked << std::endl;
}
