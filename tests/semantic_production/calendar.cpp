#include "outage_calendar.h"
#include <array>
#include <cassert>
#include <iostream>
using namespace esphome::usp_outage;
int main() {
  for (auto dates : {
       std::array<const char *, 3>{"2026-12-31","2027-01-01","2027-01-02"},
       {"2024-02-28","2024-02-29","2024-03-01"},
       {"2100-02-28","2100-03-01","2100-03-02"},
       {"2026-03-28","2026-03-29","2026-03-30"},
       {"2026-10-24","2026-10-25","2026-10-26"}}) {
    CalendarDate today{};
    assert(parse_schedule_date(dates[0], &today));
    assert(schedule_date_matches(dates[0], today, false));
    assert(schedule_date_matches(dates[1], today, true));
    assert(!schedule_date_matches(dates[0], today, true));
    assert(!schedule_date_matches(dates[1], today, false));
    assert(!schedule_date_matches(dates[2], today, true));
  }
  std::cout << "OUTAGE_CALENDAR_PRODUCTION=PASS\n";
}
