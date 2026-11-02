
#include <cassert>
#include <cstring>
#include <iostream>
#include "usp_navigation_text.h"
static const char *labels[3][8] = {
  {"Огляд","Карта","Погода","Живлення","Історія","Spotify","Годинник","Налашт."},
  {"Обзор","Карта","Погода","Питание","История","Spotify","Часы","Настройки"},
  {"Overview","Map","Weather","Power","History","Spotify","Clock","Settings"}};
static const char *modes[3][2] = {
  {"Навігація: Кнопки","Навігація: Свайпи"},
  {"Навигация: Кнопки","Навигация: Свайпы"},
  {"Navigation: Buttons","Navigation: Swipes"}};
static const char *titles[3] = {"Навігація","Навигация","Navigation"};
static const char *help[3] = {
  "Свайп вліво / вправо: екрани по колу.\nВерхній лівий кут: утримуйте 2 с для меню.",
  "Свайп влево / вправо: экраны по кругу.\nВерхний левый угол: удерживайте 2 с для меню.",
  "Swipe left / right to cycle screens.\nHold the top-left corner for 2 seconds to open the menu."};
static const char *settings[3] = {"Налаштування","Настройки","Settings"};
static const char *buttons[3] = {"Показати кнопки","Показать кнопки","Show buttons"};
static const char *close_text[3] = {"Зрозуміло","Понятно","Got it"};
int main() {
  unsigned checked = 0;
  for (unsigned language = 0; language < 256; ++language) {
    const unsigned old = language < 3 ? language : 0;
    for (unsigned page = 0; page < 8; ++page) {
      assert(std::strcmp(labels[old][page], usp_navigation_text::page_label(language, page)) == 0);
      ++checked;
    }
    for (unsigned mode = 0; mode < 2; ++mode) {
      assert(std::strcmp(modes[old][mode], usp_navigation_text::mode_label(language, mode != 0)) == 0);
      ++checked;
    }
    assert(std::strcmp(titles[old], usp_navigation_text::menu_title(language)) == 0); ++checked;
    assert(std::strcmp(help[old], usp_navigation_text::menu_help(language)) == 0); ++checked;
    assert(std::strcmp(settings[old], usp_navigation_text::menu_settings(language)) == 0); ++checked;
    assert(std::strcmp(buttons[old], usp_navigation_text::menu_buttons(language)) == 0); ++checked;
    assert(std::strcmp(close_text[old], usp_navigation_text::menu_close(language)) == 0); ++checked;
  }
  assert(checked == 3840);
  std::cout << "NAVIGATION_TEXT_CATALOG_NATIVE=PASS checked=" << checked << "\n";
}
