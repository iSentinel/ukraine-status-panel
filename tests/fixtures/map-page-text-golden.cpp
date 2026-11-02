#include "usp_map_page_text.h"
#include <cassert>
#include <cstring>
#include <iostream>
using usp_map_page_text::Label;
constexpr const char *EXPECTED[3][10] = {
  {"МАПА ТРИВОГ", "УКРАЇНА", "МАПА РЕГІОНІВ", "Дані буде підключено", "ОБРАНИЙ РЕГІОН", "КИЇВ", "Загроза", "Температура", "Радіація", "Енергомережа"},
  {"КАРТА ТРЕВОГ", "УКРАИНА", "КАРТА РЕГИОНОВ", "Данные будут подключены", "ВЫБРАННЫЙ РЕГИОН", "КИЕВ", "Угроза", "Температура", "Радиация", "Энергосеть"},
  {"ALERT MAP", "UKRAINE", "REGIONAL MAP", "Data layer will be connected", "SELECTED REGION", "KYIV", "Threat", "Temperature", "Radiation", "Grid"}
};
constexpr Label KEYS[10] = {Label::PAGE_TITLE, Label::COUNTRY, Label::REGIONAL_MAP, Label::LAYER_PLACEHOLDER, Label::SELECTED_REGION, Label::KYIV, Label::THREAT, Label::TEMPERATURE, Label::RADIATION, Label::GRID};
int main() {
  unsigned checked = 0;
  for (unsigned language = 0; language < 256; ++language) {
    for (unsigned key = 0; key < 10; ++key) {
      const auto actual = usp_map_page_text::label(language, KEYS[key]);
      assert(std::strcmp(actual.c_str(),
                         EXPECTED[language < 3 ? language : 0][key]) == 0);
      ++checked;
    }
    assert(std::strcmp(usp_map_page_text::label(
        language, static_cast<Label>(255)).c_str(), "") == 0);
  }
  std::cout << "MAP_PAGE_TEXT_NATIVE=PASS checked=" << checked << std::endl;
}
