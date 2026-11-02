#pragma once

#include <cstdint>
#include <string>

namespace esphome::usp_outage {

/** A local calendar day; arithmetic does not assume a 24-hour DST day. */
struct CalendarDate {
  uint16_t year{0};
  uint8_t month{0};
  uint8_t day{0};
};

/** Return the real number of days in a Gregorian month. */
inline uint8_t month_days(uint16_t year, uint8_t month) {
  static constexpr uint8_t DAYS[]{0, 31, 28, 31, 30, 31, 30,
                                  31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  return month == 2 && leap ? 29 : DAYS[month];
}

/** Reject impossible or unsupported dates before comparing them. */
inline bool valid_calendar_date(CalendarDate date) {
  return date.year >= 1970 && date.year <= 9999 && date.day > 0 &&
         date.day <= month_days(date.year, date.month);
}

/** Advance one calendar date across month, year and leap-day boundaries. */
inline CalendarDate following_date(CalendarDate date) {
  if (!valid_calendar_date(date)) return {};
  if (++date.day > month_days(date.year, date.month)) {
    date.day = 1;
    if (++date.month > 12) {
      date.month = 1;
      ++date.year;
    }
  }
  return valid_calendar_date(date) ? date : CalendarDate{};
}

/** Decode Yasno's local date or explicit local-midnight timestamp. */
inline bool parse_schedule_date(const std::string &text, CalendarDate *date) {
  if (date == nullptr) return false;
  if (text.size() != 10 && text.size() != 25) return false;
  if (text.size() == 25 &&
      text.compare(10, 15, "T00:00:00+02:00") != 0 &&
      text.compare(10, 15, "T00:00:00+03:00") != 0) return false;
  unsigned values[3]{};
  size_t field = 0;
  for (size_t index = 0; index < 10; ++index) {
    if (index == 4 || index == 7) {
      if (text[index] != '-') return false;
      ++field;
    } else {
      if (text[index] < '0' || text[index] > '9') return false;
      values[field] = values[field] * 10 + unsigned(text[index] - '0');
    }
  }
  CalendarDate parsed{uint16_t(values[0]), uint8_t(values[1]), uint8_t(values[2])};
  if (!valid_calendar_date(parsed)) return false;
  *date = parsed;
  return true;
}

/** Compare a provider day with today's or tomorrow's local calendar date. */
inline bool schedule_date_matches(const std::string &text, CalendarDate now,
                                  bool tomorrow) {
  if (!valid_calendar_date(now)) return false;
  if (tomorrow) now = following_date(now);
  CalendarDate provider{};
  return parse_schedule_date(text, &provider) &&
         provider.year == now.year && provider.month == now.month &&
         provider.day == now.day;
}

}  // namespace esphome::usp_outage
