#include "usp_weather_time.h"
#include "weather-time-legacy.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <string>

// Preserve the actual parser's accepted calendar range and leap-year rules.
static unsigned calendar_equivalence() {
  unsigned count = 0;
  for (unsigned year : {1969,1970,1999,2000,2004,2099,2100,2400,9999}) {
    for (unsigned month = 0; month <= 13; ++month) {
      for (unsigned day = 0; day <= 32; ++day) {
        char date[32];
        std::snprintf(date, sizeof(date), "%04u-%02u-%02u", year, month, day);
        assert(legacy::valid_date(date) == usp_weather_time::valid_date(date));
        ++count;
      }
    }
  }
  return count;
}

static unsigned time_equivalence() {
  unsigned count = 0;
  for (unsigned hour = 0; hour <= 25; ++hour) {
    for (unsigned minute = 0; minute <= 61; ++minute) {
      for (unsigned second = 0; second <= 61; ++second) {
        char value[40];
        std::snprintf(value, sizeof(value), "2024-02-29T%02u:%02u:%02u",
                      hour, minute, second);
        assert(legacy::valid_timestamp(value) == usp_weather_time::valid_timestamp(value));
        ++count;
      }
    }
  }
  return count;
}

static void captions_and_invalid_values() {
  using namespace usp_weather_time;
  assert(format_local("2026-10-23T07:15") == "23.10.2026 07:15");
  assert(format_local("2026-10-23T23:59:59") == "23.10.2026 23:59");
  assert(format_local("2000-02-29T00:00") == "29.02.2000 00:00");
  assert(as_of("", 0) == "Дані на: --");
  assert(as_of("", 1) == "Данные на: --");
  assert(as_of("", 2) == "As of: --");
  assert(as_of("2026-10-23T00:05", 2) == "As of: 23.10.2026 00:05");
  for (const char *value : {"", "n/a", "2026-02-29T12:00", "2100-02-29T12:00",
       "2026-10-23T24:00", "2026-10-23T12:60", "2026-10-23T12:00:60",
       "2026-10-23T12:00Z", "2026-10-23T12:00+03:00", "2026-10-23 12:00",
       "2026-10-23Taa:bb", "2026-00-01T12:00", "2026-12-00T12:00"}) {
    assert(!valid_timestamp(value));
    assert(format_local(value) == "--");
    assert(legacy::valid_timestamp(value) == valid_timestamp(value));
  }
}

int main() {
  const unsigned calendars = calendar_equivalence();
  const unsigned times = time_equivalence();
  captions_and_invalid_values();
  std::printf("WEATHER_TIME_NATIVE=PASS actual_legacy_calendar=%u time=%u captions3 invalid no_timezone_conversion\n",
              calendars, times);
}
