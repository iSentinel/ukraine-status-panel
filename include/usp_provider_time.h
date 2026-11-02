#pragma once
#include <cstdint>
#include <optional>
#include "usp_weather_timezone.h"
#include <string>
#include <string_view>
#include "usp_weather_time.h"

namespace usp_provider_time {
/** Read at most four decimal digits from a bounded text view. */
inline int decimal(std::string_view text, size_t start, size_t length) {
  if (length == 0 || length > 4 || start > text.size() ||
      length > text.size() - start) return -1;
  int result = 0;
  for (size_t index = start; index < start + length; ++index) {
    const char value = text[index];
    if (value < '0' || value > '9') return -1;
    result = result * 10 + value - '0';
  }
  return result;
}
/** Count Gregorian dates without a machine timezone or Unix conversion. */
inline int64_t calendar_day(std::string_view date) {
  if (date.size() != 10 || !usp_weather_time::valid_date(date)) return -1;
  const int year = decimal(date, 0, 4);
  const int month = decimal(date, 5, 2);
  const int day = decimal(date, 8, 2);
  const int prior = year - 1;
  static constexpr int before[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  int64_t result = int64_t(365) * prior + prior/4 - prior/100 + prior/400;
  result += before[month-1] + day - 1;
  if (month > 2 && year%4 == 0 && (year%100 != 0 || year%400 == 0)) ++result;
  return result;
}
/** Return a provider wall-minute counter; this is not an actual UTC instant. */
inline int64_t minute(std::string_view text) {
  if ((text.size() != 16 && text.size() != 19) || text[10] != 'T' ||
      text[13] != ':') return -1;
  const int64_t day = calendar_day(text.substr(0, 10));
  const int hour = decimal(text, 11, 2);
  const int minutes = decimal(text, 14, 2);
  if (day < 0 || hour < 0 || hour > 23 || minutes < 0 || minutes > 59) return -1;
  if (text.size() == 19) {
    const int seconds = decimal(text, 17, 2);
    if (text[16] != ':' || seconds < 0 || seconds > 59) return -1;
  }
  return day * 1440 + hour * 60 + minutes;
}
/** Convert the response's explicit fixed offset, without future DST rules. */
inline std::optional<int64_t> utc_epoch(std::string_view timestamp,
                                      int32_t offset, bool context_valid) {
  if (!context_valid || !usp_weather_timezone::valid_offset(offset)) return {};
  const int64_t wall_minute = minute(timestamp);
  if (wall_minute < 0) return {};
  constexpr int64_t UNIX_DAY = 719162;
  const int seconds = timestamp.size() == 19 ? decimal(timestamp, 17, 2) : 0;
  return (wall_minute - UNIX_DAY * 1440) * 60 + seconds - offset;
}
}  // namespace usp_provider_time
