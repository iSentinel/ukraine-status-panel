#pragma once
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

namespace usp_clock {

/** Local calendar supplied by the existing Europe/Kyiv SNTP component. */
struct Calendar {
  uint16_t year{};
  uint8_t month{}, day{}, hour{}, minute{}, weekday{};
  bool trusted{};
};

inline bool valid(const Calendar &value) {
  if (!value.trusted || value.year < 2019 || value.year > 2099 ||
      value.month < 1 || value.month > 12 || value.day < 1 ||
      value.hour > 23 || value.minute > 59 || value.weekday < 1 ||
      value.weekday > 7) return false;
  constexpr uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = value.year % 4 == 0 &&
      (value.year % 100 != 0 || value.year % 400 == 0);
  const unsigned maximum = days[value.month - 1] +
      (value.month == 2 && leap ? 1 : 0);
  return value.day <= maximum;
}

/** A local minute includes the date, so midnight and DST jumps refresh it. */
inline uint64_t minute_key(const Calendar &value) {
  if (!valid(value)) return 0;
  return (((uint64_t(value.year) * 13 + value.month) * 32 + value.day) * 24 +
          value.hour) * 60 + value.minute;
}

struct Text {
  std::string time, date, status;
  bool trusted{};
};

inline const char *pick(uint8_t language, const char *ua,
                        const char *ru, const char *en) {
  return language == 1 ? ru : (language == 2 ? en : ua);
}

/** Never turn an unsynchronised calendar into a plausible date or midnight. */
inline Text format(const Calendar &value, uint8_t language) {
  if (!valid(value))
    return {"--:--", "--.--.----",
            pick(language, "Очікування синхронізації",
                 "Ожидание синхронизации", "Waiting for time sync"), false};
  char time[16], date[40];
  std::snprintf(time, sizeof(time), "%02u:%02u", value.hour, value.minute);
  const char *weekdays[3][7] = {
      {"Нд", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"},
      {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"},
      {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}};
  const unsigned lang = language < 3 ? language : 0;
  std::snprintf(date, sizeof(date), "%s %02u.%02u.%04u",
                weekdays[lang][value.weekday - 1], value.day,
                value.month, value.year);
  return {time, date, pick(language, "Час синхронізовано",
                           "Время синхронизировано", "Time synchronised"), true};
}

/** Ignore repeat checks within the same minute, including second ticks. */
class MinuteGate {
 public:
  bool changed(const Calendar &value, uint8_t language) {
    const uint64_t key = minute_key(value) * 4 + (language < 3 ? language : 0);
    if (this->key_ == key) return false;
    this->key_ = key;
    return true;
  }
 private:
  uint64_t key_{std::numeric_limits<uint64_t>::max()};
};

inline MinuteGate &minute_gate() {
  static MinuteGate gate;
  return gate;
}

}  // namespace usp_clock
