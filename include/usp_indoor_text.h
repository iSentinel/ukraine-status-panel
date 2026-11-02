#pragma once

namespace usp_indoor_text {

enum class Key : unsigned { PREFIX, NOT_CONNECTED, WAITING, ROOM_ENTRY };

inline constexpr const char *TEXT[3][4] = {
    {"В приміщенні: ", "датчик не підключено", "очікування даних", "КІМНАТА · 24Г"},
    {"В помещении: ", "датчик не подключен", "ожидание данных", "КОМНАТА · 24Ч"},
    {"Indoor: ", "sensor not connected", "waiting for data", "ROOM · 24H"},
};

inline constexpr const char *label(unsigned language, Key key) {
  const unsigned lang = language < 3 ? language : 2;
  const unsigned index = static_cast<unsigned>(key);
  return index < 4 ? TEXT[lang][index] : "";
}

}  // namespace usp_indoor_text
