#include "usp_clock_presentation.h"
#include <cassert>
#include <iostream>
using usp_clock::Calendar;

void check_time_and_minute() {
  Calendar value{2026, 10, 20, 19, 40, 3, false};
  assert(usp_clock::format(value, 0).time == "--:--");
  assert(usp_clock::format(value, 1).date == "--.--.----");
  value.trusted = true;
  assert(usp_clock::format(value, 0).time == "19:40");
  assert(usp_clock::format(value, 0).date == "Вт 20.10.2026");
  assert(usp_clock::format(value, 2).date == "Tue 20.10.2026");
  usp_clock::MinuteGate gate;
  assert(gate.changed(value, 0));
  for (unsigned second = 0; second < 60; ++second)
    assert(!gate.changed(value, 0));
  value.minute = 41;
  assert(gate.changed(value, 0));
  assert(gate.changed(value, 1));
  assert(!gate.changed(value, 1));
}

void check_calendar_and_dst() {
  Calendar value{2026, 10, 25, 3, 59, 1, true};
  usp_clock::MinuteGate gate;
  assert(gate.changed(value, 1));
  value.minute = 0;  // Local time moves backwards at the DST transition.
  assert(gate.changed(value, 1));
  value = {2024, 2, 29, 23, 59, 5, true};
  assert(usp_clock::valid(value));
  assert(gate.changed(value, 1));
  value = {2024, 3, 1, 0, 0, 6, true};
  assert(gate.changed(value, 1));
  value = {2023, 2, 29, 12, 0, 4, true};
  assert(!usp_clock::valid(value));
  assert(usp_clock::format(value, 2).time == "--:--");
  value = {2026, 13, 1, 12, 0, 1, true};
  assert(!usp_clock::valid(value));
  value = {2026, 1, 1, 24, 0, 5, true};
  assert(!usp_clock::valid(value));
  value = {2026, 1, 1, 23, 60, 5, true};
  assert(!usp_clock::valid(value));
  value = {2026, 1, 1, 23, 59, 0, true};
  assert(!usp_clock::valid(value));
}

int main() {
  check_time_and_minute();
  check_calendar_and_dst();
  std::cout << "CLOCK_PRESENTATION=PASS untrusted minute date leap DST language\n";
}
