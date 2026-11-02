#pragma once

#include "usp_room_period.h"

namespace usp_room_period_text {

using usp_room_period::Period;

struct View {
  const char *history;
  const char *note;
  const char *temperature;
  const char *humidity;
  const char *left;
};

inline constexpr View VIEWS[4][3] = {
    {
        {"Історія 24 год", "Історія в RAM/PSRAM · після перезапуску починається заново", "ТЕМПЕРАТУРА · 24 ГОД", "ВОЛОГІСТЬ · 24 ГОД", "-24 год"},
        {"История 24 ч", "История в RAM/PSRAM · после перезапуска начинается заново", "ТЕМПЕРАТУРА · 24 Ч", "ВЛАЖНОСТЬ · 24 Ч", "-24 ч"},
        {"24h history", "RAM/PSRAM history · restarts begin a new history", "TEMPERATURE · 24H", "HUMIDITY · 24H", "-24 h"},
    },
    {
        {"Історія 7 днів", "Агрегована історія · зберігається після перезапуску", "ТЕМПЕРАТУРА · 7 ДНІВ", "ВОЛОГІСТЬ · 7 ДНІВ", "-7 днів"},
        {"История 7 дней", "Агрегированная история · сохраняется после перезапуска", "ТЕМПЕРАТУРА · 7 ДНЕЙ", "ВЛАЖНОСТЬ · 7 ДНЕЙ", "-7 дней"},
        {"7-day history", "Aggregated history · persists after restart", "TEMPERATURE · 7D", "HUMIDITY · 7D", "-7 d"},
    },
    {
        {"Історія 30 днів", "Агрегована історія · зберігається після перезапуску", "ТЕМПЕРАТУРА · 30 ДНІВ", "ВОЛОГІСТЬ · 30 ДНІВ", "-30 днів"},
        {"История 30 дней", "Агрегированная история · сохраняется после перезапуска", "ТЕМПЕРАТУРА · 30 ДНЕЙ", "ВЛАЖНОСТЬ · 30 ДНЕЙ", "-30 дней"},
        {"30-day history", "Aggregated history · persists after restart", "TEMPERATURE · 30D", "HUMIDITY · 30D", "-30 d"},
    },
    {
        {"Історія 1 року", "Агрегована історія · зберігається після перезапуску", "ТЕМПЕРАТУРА · 1 РІК", "ВОЛОГІСТЬ · 1 РІК", "-1 рік"},
        {"История 1 года", "Агрегированная история · сохраняется после перезапуска", "ТЕМПЕРАТУРА · 1 ГОД", "ВЛАЖНОСТЬ · 1 ГОД", "-1 год"},
        {"1-year history", "Aggregated history · persists after restart", "TEMPERATURE · 1Y", "HUMIDITY · 1Y", "-1 y"},
    },
};

constexpr unsigned language_index(unsigned language) {
  return language < 2 ? language : 2;
}

constexpr const View &view(unsigned language, Period period) {
  return VIEWS[static_cast<unsigned>(usp_room_period::sanitize(
      static_cast<unsigned>(period)))][language_index(language)];
}

constexpr const char *button(Period period) {
  switch (usp_room_period::sanitize(static_cast<unsigned>(period))) {
    case Period::HOURS_24:
      return "24H";
    case Period::DAYS_7:
      return "7D";
    case Period::DAYS_30:
      return "30D";
    case Period::YEAR_1:
      return "1Y";
  }
  return "24H";
}

}  // namespace usp_room_period_text
