#include "usp_power_page_text.h"
#include <cassert>
#include <cstring>
#include <iostream>
using usp_power_page_text::Label;
constexpr const char *EXPECTED[3][8] = {
  {"СТАН ЖИВЛЕННЯ", "ФАКТИЧНА МЕРЕЖА", "ОЧІКУВАННЯ", "Фізичний датчик мережі", "ГРАФІК", "НЕМАЄ ДАНИХ", "Сьогодні / Завтра / Тиждень", "ШКАЛА ЖИВЛЕННЯ"},
  {"СОСТОЯНИЕ ПИТАНИЯ", "ФАКТИЧЕСКАЯ СЕТЬ", "ОЖИДАНИЕ", "Физический датчик сети", "ГРАФИК", "НЕТ ДАННЫХ", "Сегодня / Завтра / Неделя", "ШКАЛА ПИТАНИЯ"},
  {"POWER HEALTH", "FACTUAL MAINS", "WAITING", "Physical mains sensor", "SCHEDULE", "NO DATA", "Today / Tomorrow / Week", "POWER TIMELINE"}
};
constexpr Label KEYS[8] = {Label::PAGE_TITLE, Label::FACTUAL_MAINS, Label::WAITING, Label::MAINS_SENSOR, Label::SCHEDULE, Label::NO_DATA, Label::RANGE_HINT, Label::POWER_TIMELINE};
int main() {
  unsigned checked = 0;
  for (unsigned language = 0; language < 256; ++language) {
    for (unsigned key = 0; key < 8; ++key) {
      const auto actual = usp_power_page_text::label(language, KEYS[key]);
      assert(std::strcmp(actual.c_str(),
                         EXPECTED[language < 3 ? language : 0][key]) == 0);
      ++checked;
    }
    assert(std::strcmp(usp_power_page_text::label(
        language, static_cast<Label>(8)).c_str(), "") == 0);
    assert(std::strcmp(usp_power_page_text::label(
        language, static_cast<Label>(255)).c_str(), "") == 0);
  }
  std::cout << "POWER_PAGE_TEXT_NATIVE=PASS checked=" << checked << std::endl;
}
