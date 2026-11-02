#include <cassert>
#include <cstring>
#include <iostream>
#include "usp_history_text.h"

static const char *golden[3][7] = {
  {"ІСТОРІЯ / СТАТИСТИКА", "ДОСТУПНІСТЬ ЖИВЛЕННЯ", "Факт мережі проти графіка", "ПОДІЇ ВІДКЛЮЧЕНЬ", "Історії ще немає", "ЯКІСТЬ ДАНИХ", "ОЧІКУВАННЯ"},
  {"ИСТОРИЯ / СТАТИСТИКА", "ДОСТУПНОСТЬ ПИТАНИЯ", "Факт сети против графика", "СОБЫТИЯ ОТКЛЮЧЕНИЙ", "Истории пока нет", "КАЧЕСТВО ДАННЫХ", "ОЖИДАНИЕ"},
  {"HISTORY / STATISTICS", "POWER AVAILABILITY", "Match factual mains vs schedule", "OUTAGE EVENTS", "No history yet", "DATA QUALITY", "WAITING"}
};

int main() {
  const usp_history_text::Label keys[] = {
    usp_history_text::Label::HISTORY_STATS,
    usp_history_text::Label::POWER_AVAILABILITY,
    usp_history_text::Label::MAINS_VS_SCHEDULE,
    usp_history_text::Label::OUTAGE_EVENTS,
    usp_history_text::Label::NO_HISTORY,
    usp_history_text::Label::DATA_QUALITY,
    usp_history_text::Label::WAITING,
  };
  unsigned checked = 0;
  for (unsigned language = 0; language < 256; ++language) {
    const unsigned old = language < 3 ? language : 0;
    for (unsigned index = 0; index < 7; ++index) {
      assert(std::strcmp(golden[old][index], usp_history_text::label(language, keys[index]).c_str()) == 0);
      ++checked;
    }
  }
  assert(std::strcmp("", usp_history_text::label(0, static_cast<usp_history_text::Label>(99)).c_str()) == 0);
  assert(checked == 1792);
  std::cout << "HISTORY_TEXT_CATALOG_NATIVE=PASS checked=" << checked << std::endl;
}
