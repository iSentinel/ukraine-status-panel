#include "alarm_schedule.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using usp_alarm_schedule::Clock;
using usp_alarm_schedule::Schedule;

static Clock clock_at(unsigned day, unsigned hour, unsigned minute,
                      uint32_t epoch, unsigned weekday = 4) {
  return {{2026, 10, static_cast<uint8_t>(day), static_cast<uint8_t>(hour),
           static_cast<uint8_t>(minute), static_cast<uint8_t>(weekday), true},
          epoch};
}

static Clock kyiv_spring_clock(unsigned hour, unsigned minute,
                               uint32_t epoch) {
  return {{2026, 3, 29, static_cast<uint8_t>(hour),
           static_cast<uint8_t>(minute), 7, true}, epoch};
}

static void parsing_and_storage() {
  uint16_t minutes = 99;
  assert(usp_alarm_schedule::parse_time("00:00", &minutes) && minutes == 0);
  assert(usp_alarm_schedule::parse_time("23:59", &minutes) && minutes == 1439);
  for (const char *bad : {"24:00", "23:60", "7:00", " 07:00", "07:00 ",
                          "07x00", "-1:00", "07:0a", "", "07:00:00"})
    assert(!usp_alarm_schedule::parse_time(bad, &minutes));
  assert(!usp_alarm_schedule::parse_time(nullptr, &minutes));
  assert(!usp_alarm_schedule::parse_time("07:00", nullptr));
  Schedule schedule;
  assert(schedule.active() == 0);
  for (size_t index = 0; index < 3; ++index)
    assert(!schedule.slot(index)->enabled);
  assert(!schedule.configure(3, 420, 127, true));
  assert(!schedule.configure(0, 1440, 127, true));
  assert(!schedule.configure(0, 420, 0, true));
  assert(!schedule.configure(0, 420, 128, true));
  assert(schedule.slot(3) == nullptr);
  assert(!usp_alarm_schedule::valid_day(675550101));
  auto saved = schedule.store();
  saved.slots[0].enabled = 2;
  assert(!schedule.restore(saved));
  saved = schedule.store();
  saved.slots[0].fired_day = 20260230;
  assert(!schedule.restore(saved));
  saved = schedule.store();
  saved.version = 999;
  assert(!schedule.restore(saved));
  assert(!schedule.slot(0)->enabled);
}

static void normal_and_persistent_dedup() {
  Schedule schedule;
  assert(schedule.configure(0, 420, 127, true));
  assert(schedule.configure(1, 420, 127, true));
  assert(schedule.poll(clock_at(21, 6, 59, 1792555140)) == 0);
  assert(schedule.poll(clock_at(21, 7, 0, 1792555200)) == 3);
  assert(schedule.poll(clock_at(21, 7, 0, 1792555210)) == 0);
  assert(schedule.active() == 3);
  assert(schedule.configure(0, 500, 127, true));
  assert(schedule.active_minute(0) == 420);
  assert(schedule.slot(0)->minute == 500);
  assert(schedule.configure(0, 420, 127, true));
  schedule.dismiss();
  assert(schedule.active() == 0);
  const auto saved = schedule.store();
  Schedule rebooted;
  assert(rebooted.restore(saved));
  assert(rebooted.poll(clock_at(21, 6, 59, 1792558740)) == 0);
  assert(rebooted.poll(clock_at(21, 7, 0, 1792558800)) == 0);
  assert(rebooted.configure(0, 421, 127, true));
  assert(rebooted.poll(clock_at(21, 7, 1, 1792558860)) == 0);
  assert(rebooted.poll(clock_at(22, 6, 59, 1792641540)) == 0);
  assert(rebooted.poll(clock_at(22, 7, 0, 1792641600)) == 2);
  assert(rebooted.poll(clock_at(22, 7, 1, 1792641660)) == 1);
}

static void weekdays_trust_and_jumps() {
  Schedule schedule;
  assert(schedule.configure(0, 420, 62, true));
  assert(schedule.poll(clock_at(24, 6, 59, 1792814340, 7)) == 0);
  assert(schedule.poll(clock_at(24, 7, 0, 1792814400, 7)) == 0);
  assert(schedule.configure(1, 420, 65, true));
  assert(schedule.poll(clock_at(25, 6, 59, 1792900740, 1)) == 0);
  assert(schedule.poll(clock_at(25, 7, 0, 1792900800, 1)) == 2);
  auto invalid = clock_at(26, 6, 59, 1792987140, 2);
  invalid.local.trusted = false;
  assert(schedule.poll(invalid) == 0);
  assert(schedule.poll(clock_at(26, 7, 0, 1792987200, 2)) == 0);
  assert(schedule.poll(clock_at(27, 6, 59, 1793073540, 3)) == 0);
  assert(schedule.poll(clock_at(27, 7, 0, 1793073600, 3)) == 1);
  assert(schedule.configure(2, 421, 127, true));
  assert(schedule.poll(clock_at(27, 7, 1, 1793077200, 3)) == 0);
  assert(schedule.poll(clock_at(27, 7, 2, 1793073660, 3)) == 0);
  assert(schedule.poll(clock_at(27, 7, 3, 1793073720, 3)) == 0);
}

static void future_fired_day_is_bounded() {
  Schedule schedule;
  assert(schedule.configure(0, 420, 127, true));
  auto saved = schedule.store();
  saved.slots[0].fired_day = 20261130;
  assert(schedule.restore(saved));
  assert(schedule.poll(clock_at(22, 6, 59, 1792645140, 5)) == 0);
  assert(schedule.poll(clock_at(22, 7, 0, 1792645200, 5)) == 1);
  assert(schedule.slot(0)->fired_day == 20261022);

  Schedule rollback_guard;
  saved.slots[0].fired_day = 20261023;
  assert(rollback_guard.restore(saved));
  assert(rollback_guard.poll(clock_at(22, 6, 59, 1792645140, 5)) == 0);
  assert(rollback_guard.poll(clock_at(22, 7, 0, 1792645200, 5)) == 0);

  usp_clock::Calendar month_end{2026, 10, 31, 7, 0, 7, true};
  assert(usp_alarm_schedule::next_day_key(month_end) == 20261101);
  month_end = {2028, 2, 29, 7, 0, 3, true};
  assert(usp_alarm_schedule::next_day_key(month_end) == 20280301);
}

static void repeated_dst_civil_minute() {
  Schedule schedule;
  assert(schedule.configure(0, 210, 127, true));
  assert(schedule.poll(clock_at(25, 3, 29, 1792895340, 1)) == 0);
  assert(schedule.poll(clock_at(25, 3, 30, 1792895400, 1)) == 1);
  assert(schedule.poll(clock_at(25, 3, 29, 1792898940, 1)) == 0);
  assert(schedule.poll(clock_at(25, 3, 30, 1792899000, 1)) == 0);
  assert(schedule.slot(0)->fired_day == 20261025);
}

static void spring_forward_skipped_hour() {
  Schedule schedule;
  assert(schedule.configure(0, 180, 127, true));
  assert(schedule.configure(1, 210, 127, true));
  assert(schedule.configure(2, 239, 127, true));
  assert(schedule.poll(kyiv_spring_clock(2, 59, 1774745940)) == 0);
  assert(schedule.poll(kyiv_spring_clock(4, 0, 1774746000)) == 7);
  assert(schedule.active_minute(0) == 180);
  assert(schedule.active_minute(1) == 210);
  assert(schedule.active_minute(2) == 239);
  for (size_t index = 0; index < 3; ++index)
    assert(schedule.slot(index)->fired_day == 20260329);
}

static void spring_forward_is_not_generic_catchup() {
  Schedule local_jump;
  assert(local_jump.configure(0, 210, 127, true));
  assert(local_jump.poll(kyiv_spring_clock(2, 59, 1774745940)) == 0);
  assert(local_jump.poll(kyiv_spring_clock(5, 0, 1774746000)) == 0);

  Schedule utc_jump;
  assert(utc_jump.configure(0, 210, 127, true));
  assert(utc_jump.poll(kyiv_spring_clock(2, 59, 1774745940)) == 0);
  assert(utc_jump.poll(kyiv_spring_clock(4, 0, 1774749540)) == 0);

  Schedule boot;
  assert(boot.configure(0, 210, 127, true));
  assert(boot.poll(kyiv_spring_clock(4, 0, 1774746000)) == 0);

  Schedule exact_after_gap;
  assert(exact_after_gap.configure(0, 240, 127, true));
  assert(exact_after_gap.poll(kyiv_spring_clock(2, 59, 1774745940)) == 0);
  assert(exact_after_gap.poll(kyiv_spring_clock(4, 0, 1774746000)) == 1);
}

int main() {
  parsing_and_storage();
  normal_and_persistent_dedup();
  weekdays_trust_and_jumps();
  future_fired_day_is_bounded();
  repeated_dst_civil_minute();
  spring_forward_skipped_hour();
  spring_forward_is_not_generic_catchup();
  std::puts("ALARM_SCHEDULE=PASS bounded32B disabled_defaults parse weekdays "
            "boot_sync_skip no_catchup multi_alarm edit_guard future_guard "
            "DST spring_forward reboot_dedup");
}
