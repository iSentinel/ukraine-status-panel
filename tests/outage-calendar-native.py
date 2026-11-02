"""Test actual outage accessors against calendar dates without device writes."""

import argparse
import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r"""
#include "usp_ui_i18n_0_115_107.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#if __has_include("outage_calendar.h")
#include "outage_calendar.h"
#endif
namespace esphome::usp_outage {
static uint32_t uptime = 10000;
static uint32_t millis() { return uptime; }
struct FakeTime {
  int year{2026}, month{10}, day_of_month{22};
  bool valid{true};
  bool is_valid() const { return valid; }
};
struct FakeClock {
  FakeTime value{};
  FakeTime now() const { return value; }
};
class OutageClient {
 public:
  enum class SlotType : uint8_t { UNKNOWN=0, NOT_PLANNED=1, DEFINITE=2 };
  enum class DayStatus : uint8_t {
    UNKNOWN=0, WAITING=1, SCHEDULE_APPLIES=2, EMERGENCY=3, NO_OUTAGES=4
  };
  struct OutageSlot {
    uint16_t start_min{60}, end_min{120};
    SlotType type{SlotType::DEFINITE};
  };
  struct DaySchedule {
    std::string date;
    DayStatus status{DayStatus::SCHEDULE_APPLIES};
    size_t count{1};
    std::array<OutageSlot,8> slots{};
  };
  struct Snapshot {
    bool valid{true};
    uint32_t received_ms{9000};
    DaySchedule today{"2026-10-22T00:00:00+03:00"};
    DaySchedule tomorrow{"2026-10-23T00:00:00+03:00"};
  };
  FakeClock *time_source_{nullptr};
  Snapshot snapshot_{};
  mutable std::mutex state_mutex_;
  uint32_t stale_after_ms_{2700000};
  uint32_t calendar_date_{0}, last_valid_calendar_date_{0}, last_calendar_check_ms_{0};
  bool calendar_checked_{false};
  std::atomic<bool> update_pending_{false};
  int refresh_requests{0};
  void request_refresh() { ++refresh_requests; }
  void check_calendar_();
  static uint16_t outage_minutes_(const DaySchedule &schedule);
  uint16_t today_outage_minutes() const;
  uint16_t tomorrow_outage_minutes() const;
  bool tomorrow_valid() const;
  bool schedule_valid() const;
  bool schedule_stale() const;
  uint8_t today_status() const;
  uint8_t tomorrow_status() const;
  std::string today_date() const;
  std::string tomorrow_date() const;
  size_t today_slot_count() const;
  uint16_t today_slot_start(size_t index) const;
  uint16_t today_slot_end(size_t index) const;
  uint8_t today_slot_type(size_t index) const;
  int16_t next_outage_start(int16_t current_minute) const;
  int16_t next_outage_end(int16_t current_minute) const;
  bool day_current_(const DaySchedule &day, bool tomorrow) const;
};
"""
METHODS = (
    "tomorrow_valid",
    "schedule_valid",
    "schedule_stale",
    "today_status",
    "tomorrow_status",
    "today_date",
    "tomorrow_date",
    "today_slot_count",
    "today_slot_start",
    "today_slot_end",
    "today_slot_type",
    "next_outage_start",
    "next_outage_end",
    "today_outage_minutes",
    "tomorrow_outage_minutes",
    "outage_minutes_",
    "check_calendar_",
)
CASES = r"""
}
using namespace esphome::usp_outage;
static void no_current_data(OutageClient &client) {
  assert(!client.schedule_valid() && client.schedule_stale());
  assert(client.today_status() == 0 && client.today_date().empty());
  assert(client.today_slot_count() == 0);
  assert(client.today_slot_start(0) == 0 && client.today_slot_end(0) == 0);
  assert(client.today_slot_type(0) == 0 && client.today_outage_minutes() == 0);
  assert(client.next_outage_start(30) == -1);
  assert(client.next_outage_end(90) == -1);
}
static void accessors() {
  FakeClock clock;
  OutageClient client;
  no_current_data(client);
  client.time_source_ = &clock;
  assert(client.schedule_valid() && !client.schedule_stale());
  assert(client.today_slot_count() == 1 && client.today_outage_minutes() == 60);
  assert(client.tomorrow_outage_minutes() == 60);
  assert(client.next_outage_start(30) == 60);
  assert(client.next_outage_end(90) == 120);
  assert(client.next_outage_start(-1) == -1);
  assert(client.next_outage_end(1440) == -1);
  for (const char *date : {"2026-10-21T00:00:00+03:00", "2026-10-23",
       "2026-10-22T12:00:00+03:00", "2026-13-22", "2026-10-22Z",
       "2026-10-22T00:00:00+00:00", "2026-10-22T00:00:00+03:00evil", ""}) {
    client.snapshot_.today.date = date;
    no_current_data(client);
  }
  client.snapshot_.today.date = "2026-10-22";
  assert(client.schedule_valid());
  clock.value.valid = false;
  no_current_data(client);
  clock.value.valid = true;
  client.snapshot_.valid = false;
  no_current_data(client);
}
static void tomorrow_and_freshness() {
  FakeClock clock;
  OutageClient client;
  client.time_source_ = &clock;
  client.snapshot_.tomorrow.date = "2026-10-22";
  assert(!client.tomorrow_valid());
  assert(client.tomorrow_status() == 0 && client.tomorrow_date().empty());
  assert(client.tomorrow_outage_minutes() == 0);
  client.snapshot_.tomorrow.date = "2026-10-23";
  assert(client.tomorrow_valid() && client.tomorrow_status() == 2);
  const uint32_t saved = uptime;
  uptime = client.snapshot_.received_ms + client.stale_after_ms_;
  assert(client.schedule_valid() && !client.schedule_stale());
  ++uptime;
  assert(client.schedule_valid() && client.schedule_stale());
  uptime = 1000;
  client.snapshot_.received_ms = UINT32_MAX - 1000;
  assert(!client.schedule_stale());
  uptime = saved;
}
static void calendar_boundaries() {
  for (auto dates : {std::array<const char *,3>{"2026-12-31","2027-01-01","2027-01-02"},
       {"2024-02-28","2024-02-29","2024-03-01"},
       {"2026-02-28","2026-03-01","2026-03-02"},
       {"2026-03-28","2026-03-29","2026-03-30"},
       {"2026-10-24","2026-10-25","2026-10-26"},
       {"2000-02-28","2000-02-29","2000-03-01"},
       {"2100-02-28","2100-03-01","2100-03-02"}}) {
    CalendarDate day{};
    assert(parse_schedule_date(dates[0], &day));
    assert(schedule_date_matches(dates[0], day, false));
    assert(schedule_date_matches(dates[1], day, true));
    assert(!schedule_date_matches(dates[2], day, true));
  }
  CalendarDate day{};
  for (const char *invalid : {"2026-02-29", "2024-02-30", "2026-00-01",
       "2026-01-00", "0000-01-01", "2026/10/22", "2026-1a-22"})
    assert(!parse_schedule_date(invalid, &day));
  assert(!valid_calendar_date(following_date({9999,12,31})));
}
static void date_change_requests() {
  FakeClock clock;
  OutageClient client;
  client.time_source_ = &clock;
  client.check_calendar_();
  assert(client.refresh_requests == 0);
  uptime += 1000;
  clock.value.day_of_month = 23;
  client.check_calendar_();
  assert(client.refresh_requests == 1);
  no_current_data(client);
  uptime += 1000;
  client.check_calendar_();
  assert(client.refresh_requests == 1);
  uptime += 1000;
  clock.value.valid = false;
  client.check_calendar_();
  assert(client.calendar_date_ == 0);
  uptime += 1000;
  clock.value.valid = true;
  client.check_calendar_();
  assert(client.refresh_requests == 2);
  uptime += 1000;
  clock.value.day_of_month = 22;
  client.check_calendar_();
  assert(client.refresh_requests == 3 && client.schedule_valid());
  client.last_calendar_check_ms_ = UINT32_MAX - 500;
  uptime = 500;
  clock.value.day_of_month = 23;
  client.check_calendar_();
  assert(client.refresh_requests == 4);
}
int main() {
  const char *expected[]{
    "Сьогодні 1 год 0 хв · Завтра --",
    "Сегодня 1 ч 0 мин · Завтра --",
    "Today 1h0m · Tomorrow --"};
  for (uint8_t language = 0; language < 3; ++language)
    assert(usp_ui_i18n::outage_day_summary(60, -1, language) == expected[language]);
  assert(usp_ui_i18n::outage_day_summary(0, 0, 2) == "Today 0h0m · Tomorrow 0h0m");
  accessors();
  tomorrow_and_freshness();
  calendar_boundaries();
  date_change_requests();
  std::cout << "OUTAGE_CALENDAR_ACCESSORS=PASS midnight month year leap "
               "DST_dates invalid_clock stale rollover one_refresh" << std::endl;
}
"""


def method(source, name):
    """Extract a complete production method by its brace boundaries."""
    marker = "OutageClient::" + name + "("
    position = source.index(marker)
    start = source.rfind("\n", 0, position) + 1
    opened = source.index("{", position)
    depth = 1
    end = opened + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def native_source():
    """Use the production accessors rather than a model of their decisions."""
    source = (ROOT / "components/usp_outage/outage_client.cpp").read_text()
    methods = list(METHODS)
    if "OutageClient::day_current_(" in source:
        methods.append("day_current_")
    return FIXTURE + "".join(method(source, name) for name in methods) + CASES


def run_native():
    """Execute isolated accessors with controlled clock and snapshot fixtures."""
    with tempfile.TemporaryDirectory(prefix="usp-outage-date-") as name:
        work = Path(name)
        source, binary = work / "test.cpp", work / "test"
        source.write_text(native_source())
        command = [
            "g++",
            "-std=c++20",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-fno-pie",
            "-no-pie",
            "-pthread",
            "-I",
            str(ROOT / "components/usp_outage"),
            "-I",
            str(ROOT / "include"),
            str(source),
            "-o",
            str(binary),
        ]
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary)], check=False)
        logger.info("Production outage accessors exit: {}", result.returncode)
        return result.returncode


def main():
    """Run a server-only check; never change the panel clock or schedule."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    raise SystemExit(0 if run_native() == 0 else 1)


if __name__ == "__main__":
    main()
