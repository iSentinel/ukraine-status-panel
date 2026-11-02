#pragma once

namespace usp_navigation_text {

inline constexpr const char *LABELS[3][8] = {
    {"Огляд", "Карта", "Погода", "Живлення", "Історія", "Spotify", "Годинник", "Налашт."},
    {"Обзор", "Карта", "Погода", "Питание", "История", "Spotify", "Часы", "Настройки"},
    {"Overview", "Map", "Weather", "Power", "History", "Spotify", "Clock", "Settings"},
};
inline constexpr const char *MODES[3][2] = {
    {"Навігація: Кнопки", "Навігація: Свайпи"},
    {"Навигация: Кнопки", "Навигация: Свайпы"},
    {"Navigation: Buttons", "Navigation: Swipes"},
};
inline constexpr const char *TITLES[] = {"Навігація", "Навигация", "Navigation"};
inline constexpr const char *HELP[] = {
    "Свайп вліво / вправо: екрани по колу.\nВерхній лівий кут: утримуйте 2 с для меню.",
    "Свайп влево / вправо: экраны по кругу.\nВерхний левый угол: удерживайте 2 с для меню.",
    "Swipe left / right to cycle screens.\nHold the top-left corner for 2 seconds to open the menu.",
};
inline constexpr const char *SETTINGS[] = {"Налаштування", "Настройки", "Settings"};
inline constexpr const char *BUTTONS[] = {"Показати кнопки", "Показать кнопки", "Show buttons"};
inline constexpr const char *CLOSE[] = {"Зрозуміло", "Понятно", "Got it"};

/** Keep the established navigation fallback: invalid language selects Ukrainian. */
inline unsigned language_index(unsigned language) {
  return language < 3 ? language : 0;
}

/** Return a page caption in the existing eight-page navigation order. */
inline const char *page_label(unsigned language, unsigned page) {
  return page < 8 ? LABELS[language_index(language)][page] : "";
}

/** Return the existing Buttons/Swipes navigation mode caption. */
inline const char *mode_label(unsigned language, bool fullscreen) {
  return MODES[language_index(language)][fullscreen];
}

inline const char *menu_title(unsigned language) {
  return TITLES[language_index(language)];
}

inline const char *menu_help(unsigned language) {
  return HELP[language_index(language)];
}

inline const char *menu_settings(unsigned language) {
  return SETTINGS[language_index(language)];
}

inline const char *menu_buttons(unsigned language) {
  return BUTTONS[language_index(language)];
}

inline const char *menu_close(unsigned language) {
  return CLOSE[language_index(language)];
}

}  // namespace usp_navigation_text
