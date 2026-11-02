#include "usp_home_page_text.h"
#include <cassert>
#include <cstring>
#include <iostream>
using usp_home_page_text::Label;
constexpr const char *EXPECTED[3][25] = {
  {"СТАТУС", "ТРИВОГИ / РЕГІОН", "КИЇВ", "МАПА", "СТАН ЖИВЛЕННЯ", "Мережа / графік", "ІНТЕРНЕТ", "ПІДКЛЮЧЕНО", "Стан провайдера", "ВІДКЛЮЧЕННЯ", "НЕМАЄ ДАНИХ", "Графік / факт", "ПОГОДА", "4 ДНІ", "Мапа України", "ПОВНА", "Активні тривоги", "Живлення · Shelly", "Погода · Кімната", "Графік відключень", "Живлення від мережі", "Акумулятор", "Wi-Fi", "Група Yasno", "Мережа"},
  {"СТАТУС", "ТРЕВОГИ / РЕГИОН", "КИЕВ", "КАРТА", "СОСТОЯНИЕ ПИТАНИЯ", "Сеть / график", "ИНТЕРНЕТ", "ПОДКЛЮЧЕНО", "Состояние провайдера", "ОТКЛЮЧЕНИЯ", "НЕТ ДАННЫХ", "График / факт", "ПОГОДА", "4 ДНЯ", "Карта Украины", "ПОЛНАЯ", "Активные тревоги", "Питание · Shelly", "Погода · Комната", "График отключений", "Питание от сети", "Батарея", "Wi-Fi", "Группа Yasno", "Сеть"},
  {"STATUS", "ALERTS / REGION", "KYIV", "MAP", "POWER HEALTH", "Mains / schedule", "INTERNET", "CONNECTED", "Provider health", "OUTAGES", "NO DATA", "Schedule / factual", "WEATHER", "4 DAYS", "Map of Ukraine", "FULL", "Active Alerts", "Power · Shelly", "Weather · Room", "Outage Schedule", "Mains power", "Battery", "Wi-Fi", "Yasno group", "Mains"}
};
constexpr Label KEYS[25] = {Label::PAGE_TITLE, Label::ALERT_REGION, Label::KYIV, Label::MAP, Label::POWER_HEALTH, Label::MAINS_SCHEDULE, Label::INTERNET, Label::CONNECTED, Label::PROVIDER_HEALTH, Label::OUTAGES, Label::NO_DATA, Label::SCHEDULE_FACT, Label::WEATHER, Label::FOUR_DAYS, Label::MAP_TITLE, Label::MAP_EXPAND, Label::ALERTS_TITLE, Label::POWER_TITLE, Label::HISTORY_TITLE, Label::OUTAGE_TITLE, Label::MAINS_CAPTION, Label::BATTERY_CAPTION, Label::WIFI_CAPTION, Label::YASNO_GROUP, Label::MAINS_HEADER};
int main() {
  unsigned checked = 0;
  for (unsigned language = 0; language < 256; ++language) {
    for (unsigned key = 0; key < 25; ++key) {
      const auto actual = usp_home_page_text::label(language, KEYS[key]);
      assert(std::strcmp(actual.c_str(),
                         EXPECTED[language < 3 ? language : 0][key]) == 0);
      ++checked;
    }
    assert(std::strcmp(usp_home_page_text::label(
        language, static_cast<Label>(25)).c_str(), "") == 0);
    assert(std::strcmp(usp_home_page_text::label(
        language, static_cast<Label>(255)).c_str(), "") == 0);
  }
  std::cout << "HOME_PAGE_TEXT_NATIVE=PASS checked=" << checked << std::endl;
}
