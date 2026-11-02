#pragma once

#include <string>
#include <string_view>

namespace usp_weather_time {

// Only short fixed-width provider fields are parsed; malformed text is refused.
inline bool decimal(std::string_view text, unsigned *value) {
  if (text.empty() || text.size() > 4) return false;
  unsigned result = 0;
  for (char digit : text) {
    if (digit < '0' || digit > '9') return false;
    result = result * 10U + static_cast<unsigned>(digit - '0');
  }
  *value = result;
  return true;
}

inline bool valid_date(std::string_view text) {
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
  unsigned year, month, day;
  if (!decimal(text.substr(0, 4), &year) || year < 1970 ||
      !decimal(text.substr(5, 2), &month) || month < 1 || month > 12 ||
      !decimal(text.substr(8, 2), &day) || day < 1) return false;
  constexpr unsigned DAYS[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  return day <= DAYS[month - 1] + (month == 2 && leap ? 1U : 0U);
}

inline bool valid_timestamp(std::string_view text) {
  if ((text.size() != 16 && text.size() != 19) ||
      !valid_date(text.substr(0, 10)) || text[10] != 'T' || text[13] != ':')
    return false;
  unsigned hour, minute, second;
  if (!decimal(text.substr(11, 2), &hour) || hour > 23 ||
      !decimal(text.substr(14, 2), &minute) || minute > 59) return false;
  return text.size() == 16 ||
      (text[16] == ':' && decimal(text.substr(17, 2), &second) && second <= 59);
}

// Keep the provider's city-local time; this function does not convert time zones.
inline std::string format_local(std::string_view text) {
  if (!valid_timestamp(text)) return "--";
  return std::string(text.substr(8, 2)) + "." +
      std::string(text.substr(5, 2)) + "." + std::string(text.substr(0, 4)) +
      " " + std::string(text.substr(11, 5));
}

// current.time describes the weather values, rather than the download instant.
inline std::string as_of(std::string_view text, unsigned language) {
  const char *prefix = language == 0 ? "Дані на: " :
      language == 1 ? "Данные на: " : "As of: ";
  return std::string(prefix) + format_local(text);
}
}  // namespace usp_weather_time
