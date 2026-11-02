#include <cassert>
#include <cstring>
#include <iostream>
#include "usp_diagnostics_text.h"

using usp_diagnostics_text::Label;

int main() {
  const char *expected[3][11] = {
    {"ДІАГНОСТИКА СИСТЕМИ", "НАЗАД", "ОБЛАДНАННЯ", "RGB екран", "DMA-буфер", "20 рядків", "Сенсор", "Опитування 50 мс", "РОБОТА", "ТЕСТ ДОТИКУ", "ДОТИК: OK"},
    {"ДИАГНОСТИКА СИСТЕМЫ", "НАЗАД", "ОБОРУДОВАНИЕ", "RGB экран", "DMA-буфер", "20 строк", "Сенсор", "Опрос 50 мс", "РАБОТА", "ТЕСТ КАСАНИЯ", "КАСАНИЕ: OK"},
    {"SYSTEM DIAGNOSTICS", "BACK", "HARDWARE", "RGB display", "Bounce buffer", "20 lines", "Touch", "Polling 50 ms", "RUNTIME", "TOUCH TEST", "TOUCH: OK"}
  };
  unsigned comparisons = 0;
  for (unsigned language = 0; language < 256; ++language) {
    const unsigned expected_language = language < 3 ? language : 0;
    for (unsigned key = 0; key < 11; ++key) {
      const auto actual = usp_diagnostics_text::label(language, static_cast<Label>(key));
      assert(std::strcmp(actual.c_str(), expected[expected_language][key]) == 0);
      ++comparisons;
    }
  }
  assert(std::strcmp(usp_diagnostics_text::label(0, static_cast<Label>(99)).c_str(), "") == 0);
  std::cout << "DIAGNOSTICS_TEXT_CATALOG_NATIVE=PASS checked=" << comparisons << "\n";
}
