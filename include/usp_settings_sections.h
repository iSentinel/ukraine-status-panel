#pragma once

#include <lvgl.h>
#include <cstddef>
#include <cstdint>

namespace usp_settings_sections {

constexpr unsigned COUNT = 6;
constexpr const char *NAMES[3][COUNT] = {
    {"Екран", "Мова", "Регіон", "Звук", "Мережа", "Система"},
    {"Экран", "Язык", "Регион", "Звук", "Сеть", "Система"},
    {"Display", "Language", "Region", "Sound", "Network", "System"},
};
constexpr const char *NOTES[3][4] = {
    {"Мова спільна для екрана і вебінтерфейсу.",
     "Відкрийте карту й торкніться своєї області.",
     "Будильники відкриваються на екрані годинника. Звук типово вимкнений.",
     "Поточне підключення Wi-Fi та локальна адреса панелі."},
    {"Язык общий для экрана и веб-интерфейса.",
     "Откройте карту и коснитесь своей области.",
     "Будильники открываются на экране часов. Звук по умолчанию выключен.",
     "Текущее подключение Wi-Fi и локальный адрес панели."},
    {"Language is shared by the display and web interface.",
     "Open the map and tap your region.",
     "Open alarms from the Clock screen. Sound is disabled by default.",
     "Current Wi-Fi connection and the panel's local address."},
};

/** Keep the section choice local to this screen; invalid values open Display. */
constexpr unsigned sanitize(unsigned value) { return value < COUNT ? value : 0; }
constexpr unsigned language(unsigned value) { return value < 3 ? value : 0; }

constexpr const char *name(unsigned lang, unsigned section) {
  return NAMES[language(lang)][sanitize(section)];
}
constexpr const char *note(unsigned lang, unsigned index) {
  return index < 4 ? NOTES[language(lang)][index] : "";
}
constexpr const char *choose_region(unsigned lang) {
  constexpr const char *labels[] = {
      "Вибрати на карті", "Выбрать на карте", "Choose on map"};
  return labels[language(lang)];
}
constexpr const char *network(unsigned lang, bool connected) {
  constexpr const char *labels[3][2] = {
      {"Wi-Fi не підключено", "Wi-Fi підключено"},
      {"Wi-Fi не подключён", "Wi-Fi подключён"},
      {"Wi-Fi disconnected", "Wi-Fi connected"}};
  return labels[language(lang)][connected ? 1 : 0];
}

/** Reveal one panel and distinguish its button by both fill and border. */
inline void apply(lv_obj_t *const *panels, lv_obj_t *const *buttons,
                  unsigned selected) {
  selected = sanitize(selected);
  for (unsigned index = 0; index < COUNT; ++index) {
    const bool active = index == selected;
    if (panels[index] != nullptr) {
      if (active) lv_obj_remove_flag(panels[index], LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(panels[index], LV_OBJ_FLAG_HIDDEN);
    }
    if (buttons[index] == nullptr) continue;
    lv_obj_set_style_bg_color(buttons[index],
        lv_color_hex(active ? 0x16263A : 0x0C1828), LV_PART_MAIN);
    lv_obj_set_style_border_color(buttons[index],
        lv_color_hex(active ? 0x60A5FA : 0x263D57), LV_PART_MAIN);
    lv_obj_set_style_border_width(buttons[index], active ? 2 : 1, LV_PART_MAIN);
  }
}

}  // namespace usp_settings_sections
