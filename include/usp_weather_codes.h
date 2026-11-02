#pragma once

#include <cstdint>
#include <string_view>

// Source: https://open-meteo.com/en/docs (WMO table checked 2026-10-23).
// Broad groups stay compatible; exact labels retain ice, hail and intensity.
namespace usp_weather_codes {
enum Condition : uint8_t {
  CLEAR, PARTLY_CLOUDY, OVERCAST, FOG, DRIZZLE, RAIN,
  SNOW, SHOWERS, SNOW_SHOWERS, THUNDERSTORM, UNKNOWN
};

enum class Intensity : uint8_t { NONE, LIGHT, MODERATE, DENSE, HEAVY, VIOLENT };

struct Definition {
  uint8_t code;
  Condition family;
  // For codes 96/99 this is hail intensity, not the thunderstorm strength.
  Intensity intensity;
  bool ice_related;
  bool hail;
  const char *text[3];
  const char *compact[3];
};

inline constexpr Definition DEFINITIONS[] = {
  {0, CLEAR, Intensity::NONE, false, false,
   {"Ясно", "Ясно", "Clear"},
   {"Ясно", "Ясно", "Clear"}},
  {1, PARTLY_CLOUDY, Intensity::NONE, false, false,
   {"Майже ясно", "Малооблачно", "Mainly clear"},
   {"Майже ясно", "Малооблачно", "Mainly clear"}},
  {2, PARTLY_CLOUDY, Intensity::NONE, false, false,
   {"Мінлива хмарність", "Переменная облачность", "Partly cloudy"},
   {"Мінлива хмарність", "Перем. облачность", "Partly cloudy"}},
  {3, OVERCAST, Intensity::NONE, false, false,
   {"Хмарно", "Облачно", "Overcast"},
   {"Хмарно", "Облачно", "Overcast"}},
  {45, FOG, Intensity::NONE, false, false,
   {"Туман", "Туман", "Fog"},
   {"Туман", "Туман", "Fog"}},
  {48, FOG, Intensity::NONE, true, false,
   {"Туман з памороззю", "Туман с изморозью", "Rime fog"},
   {"Туман, паморозь", "Туман, изморозь", "Rime fog"}},
  {51, DRIZZLE, Intensity::LIGHT, false, false,
   {"Легка мряка", "Слабая морось", "Light drizzle"},
   {"Легка мряка", "Слабая морось", "Light drizzle"}},
  {53, DRIZZLE, Intensity::MODERATE, false, false,
   {"Помірна мряка", "Умеренная морось", "Moderate drizzle"},
   {"Помірна мряка", "Умеренная морось", "Moderate drizzle"}},
  {55, DRIZZLE, Intensity::DENSE, false, false,
   {"Густа мряка", "Густая морось", "Dense drizzle"},
   {"Густа мряка", "Густая морось", "Dense drizzle"}},
  {56, DRIZZLE, Intensity::LIGHT, true, false,
   {"Легка крижана мряка", "Слабая ледяная морось", "Light freezing drizzle"},
   {"Легка криж. мряка", "Слаб. лед. морось", "Light icy drizzle"}},
  {57, DRIZZLE, Intensity::DENSE, true, false,
   {"Густа крижана мряка", "Густая ледяная морось", "Dense freezing drizzle"},
   {"Густа криж. мряка", "Густ. лед. морось", "Dense icy drizzle"}},
  {61, RAIN, Intensity::LIGHT, false, false,
   {"Невеликий дощ", "Небольшой дождь", "Light rain"},
   {"Невеликий дощ", "Небольшой дождь", "Light rain"}},
  {63, RAIN, Intensity::MODERATE, false, false,
   {"Помірний дощ", "Умеренный дождь", "Moderate rain"},
   {"Помірний дощ", "Умеренный дождь", "Moderate rain"}},
  {65, RAIN, Intensity::HEAVY, false, false,
   {"Сильний дощ", "Сильный дождь", "Heavy rain"},
   {"Сильний дощ", "Сильный дождь", "Heavy rain"}},
  {66, RAIN, Intensity::LIGHT, true, false,
   {"Легкий крижаний дощ", "Слабый ледяной дождь", "Light freezing rain"},
   {"Легкий криж. дощ", "Слаб. лед. дождь", "Light icy rain"}},
  {67, RAIN, Intensity::HEAVY, true, false,
   {"Сильний крижаний дощ", "Сильный ледяной дождь", "Heavy freezing rain"},
   {"Сильн. криж. дощ", "Сильн. лед. дождь", "Heavy icy rain"}},
  {71, SNOW, Intensity::LIGHT, false, false,
   {"Невеликий сніг", "Небольшой снег", "Light snow"},
   {"Невеликий сніг", "Небольшой снег", "Light snow"}},
  {73, SNOW, Intensity::MODERATE, false, false,
   {"Помірний сніг", "Умеренный снег", "Moderate snow"},
   {"Помірний сніг", "Умеренный снег", "Moderate snow"}},
  {75, SNOW, Intensity::HEAVY, false, false,
   {"Сильний сніг", "Сильный снег", "Heavy snow"},
   {"Сильний сніг", "Сильный снег", "Heavy snow"}},
  {77, SNOW, Intensity::NONE, false, false,
   {"Сніжна крупа", "Снежная крупа", "Snow grains"},
   {"Сніжна крупа", "Снежная крупа", "Snow grains"}},
  {80, SHOWERS, Intensity::LIGHT, false, false,
   {"Невеликі зливи", "Слабые ливни", "Light showers"},
   {"Невеликі зливи", "Слабые ливни", "Light showers"}},
  {81, SHOWERS, Intensity::MODERATE, false, false,
   {"Помірні зливи", "Умеренные ливни", "Moderate showers"},
   {"Помірні зливи", "Умеренные ливни", "Moderate showers"}},
  {82, SHOWERS, Intensity::VIOLENT, false, false,
   {"Дуже сильні зливи", "Очень сильные ливни", "Violent showers"},
   {"Сильна злива", "Сильный ливень", "Violent showers"}},
  {85, SNOW_SHOWERS, Intensity::LIGHT, false, false,
   {"Невеликі снігові заряди", "Слабые снежные заряды", "Light snow showers"},
   {"Легк. сніг. заряд", "Слаб. снеж. заряд", "Light snow showers"}},
  {86, SNOW_SHOWERS, Intensity::HEAVY, false, false,
   {"Сильні снігові заряди", "Сильные снежные заряды", "Heavy snow showers"},
   {"Сильн. сніг. заряд", "Сильн. снеж. заряд", "Heavy snow shower"}},
  {95, THUNDERSTORM, Intensity::NONE, false, false,
   {"Гроза", "Гроза", "Thunderstorm"},
   {"Гроза", "Гроза", "Thunderstorm"}},
  {96, THUNDERSTORM, Intensity::LIGHT, false, true,
   {"Гроза зі слабким градом", "Гроза со слабым градом", "Thunderstorm with light hail"},
   {"Гроза, слаб. град", "Гроза, слаб. град", "Thunder, light hail"}},
  {97, THUNDERSTORM, Intensity::HEAVY, false, false,
   {"Сильна гроза", "Сильная гроза", "Heavy thunderstorm"},
   {"Сильна гроза", "Сильная гроза", "Heavy storm"}},
  {99, THUNDERSTORM, Intensity::HEAVY, false, true,
   {"Гроза з сильним градом", "Гроза с сильным градом", "Thunderstorm with heavy hail"},
   {"Гроза, сильн. град", "Гроза, сильн. град", "Thunder, heavy hail"}},
};

// A short bounded scan keeps classification and labels on the same definition.
constexpr const Definition *lookup(uint8_t code) {
  for (const auto &item : DEFINITIONS) {
    if (item.code == code) return &item;
  }
  return nullptr;
}

constexpr Condition group(uint8_t code) {
  const auto *item = lookup(code);
  return item == nullptr ? UNKNOWN : item->family;
}

constexpr bool has_rain(uint8_t code) {
  const auto value = group(code);
  return value == DRIZZLE || value == RAIN || value == SHOWERS;
}

constexpr bool has_snow(uint8_t code) {
  const auto value = group(code);
  return value == SNOW || value == SNOW_SHOWERS;
}

constexpr bool is_ice_related(uint8_t code) {
  const auto *item = lookup(code);
  return item != nullptr && item->ice_related;
}

constexpr bool has_hail(uint8_t code) {
  const auto *item = lookup(code);
  return item != nullptr && item->hail;
}

constexpr Intensity intensity(uint8_t code) {
  const auto *item = lookup(code);
  return item == nullptr ? Intensity::NONE : item->intensity;
}

inline const char *description(uint8_t code, uint8_t language) {
  const auto *item = lookup(code);
  const uint8_t lang = language <= 2 ? language : 2;
  static constexpr const char *UNKNOWN_TEXT[] = {"Невідомо", "Неизвестно", "Unknown"};
  return item == nullptr ? UNKNOWN_TEXT[lang] : item->text[lang];
}

// Match only known full descriptions; never shorten arbitrary user text.
inline const char *compact_description(std::string_view text, uint8_t language) {
  const uint8_t lang = language <= 2 ? language : 2;
  for (const auto &item : DEFINITIONS) {
    if (text == item.text[lang]) return item.compact[lang];
  }
  return nullptr;
}
}  // namespace usp_weather_codes
