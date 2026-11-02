#include "usp_room_period_chart.h"

#include "esp_heap_caps.h"
#include "esphome/core/helpers.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using esphome::usp_room_aggregate_history::RoomAggregateHistory;
using usp_room_chart::Model;
using usp_room_period::Period;

uint32_t esphome::host_millis = 1000;

namespace {

void reset_host() {
  esphome::host_preferences.persisted.clear();
  esphome::host_preferences.pending.clear();
  esphome::host_preferences.save_calls = 0;
  esphome::host_preferences.sync_calls = 0;
  esphome::host_preferences.fail_save = false;
  esphome::host_preferences.fail_sync = false;
  host_alloc_fail_after = -1;
  host_alloc_calls = 0;
  esphome::host_millis = 1000;
  assert(host_live_bytes == 0);
}

void date_fields(size_t day, uint16_t *year, uint16_t *doy) {
  if (day < 366U) {
    *year = 2024;
    *doy = static_cast<uint16_t>(day + 1U);
    return;
  }
  *year = 2025;
  *doy = static_cast<uint16_t>(day - 366U + 1U);
}

uint32_t day_key(size_t day) {
  uint16_t year = 0;
  uint16_t doy = 0;
  date_fields(day, &year, &doy);
  return RoomAggregateHistory::local_day_key(year, doy);
}

bool valid_sample(size_t day, uint8_t hour) {
  if (day == 390U) return false;
  if (day == 397U && hour == 10U) return false;
  return true;
}

void fill_400_days(RoomAggregateHistory *history) {
  uint32_t epoch = 1704067200U;
  for (size_t day = 0; day < 400U; ++day) {
    uint16_t year = 0;
    uint16_t doy = 0;
    date_fields(day, &year, &doy);
    for (uint8_t hour = 0; hour < 24U; ++hour) {
      for (uint8_t slot = 0; slot < 12U; ++slot) {
        const bool valid = valid_sample(day, hour);
        const float temperature = 18.0f + static_cast<float>((day + hour) % 90U) / 10.0f;
        const float humidity = 35.0f + static_cast<float>((hour * 3U + slot) % 500U) / 10.0f;
        assert(history->record(epoch, year, doy, hour, temperature, humidity, valid));
        epoch += 300U;
        esphome::host_millis += 300000U;
        history->loop();
      }
    }
  }
  assert(history->flush());
}

void check_bounds(const Model &model) {
  assert(model.point_count <= usp_room_chart::MAX_POINTS);
  assert(model.segment_count <= usp_room_chart::MAX_SEGMENTS);
  for (uint16_t index = 0; index < model.point_count; ++index) {
    assert(model.temperature[index].x >= 0 && model.temperature[index].x <= 420);
    assert(model.temperature[index].y >= 0 && model.temperature[index].y <= 120);
    assert(model.humidity[index].x >= 0 && model.humidity[index].x <= 420);
    assert(model.humidity[index].y >= 0 && model.humidity[index].y <= 120);
  }
}

void check_main_windows() {
  reset_host();
  {
    RoomAggregateHistory history;
    history.setup();
    fill_400_days(&history);
    assert(history.hourly_count() == 768U);
    assert(history.daily_count() == 372U);

    const uint32_t latest_day = day_key(399U);
    const uint32_t latest_hour = latest_day * 24U + 23U;
    Model week{};
    usp_room_period_chart::build(history, Period::DAYS_7, latest_hour,
                                     420, 120, 120, &week);
    assert(week.point_count == 167U);
    assert(week.segment_count == 2U);
    assert(week.dropped_runs == 0U);
    assert(week.valid_count == 2004U);
    assert(week.gap_count == 12U);
    assert(week.temperature[0].x == 0);
    assert(week.temperature[week.point_count - 1U].x == 420);
    check_bounds(week);

    Model month{};
    usp_room_period_chart::build(history, Period::DAYS_30, latest_day,
                                     420, 120, 120, &month);
    assert(month.point_count == 29U);
    assert(month.segment_count == 2U);
    assert(month.dropped_runs == 0U);
    assert(month.valid_count == 8340U);
    assert(month.gap_count == 300U);
    check_bounds(month);

    Model year{};
    usp_room_period_chart::build(history, Period::YEAR_1, latest_day,
                                     420, 120, 120, &year);
    assert(year.point_count == 365U);
    assert(year.segment_count == 2U);
    assert(year.dropped_runs == 0U);
    assert(year.valid_count == 105108U);
    assert(year.valid_count > 65535U);
    assert(year.gap_count == 300U);
    assert(year.temperature[0].x == 0);
    assert(year.temperature[year.point_count - 1U].x == 420);
    check_bounds(year);
  }
  assert(host_live_bytes == 0);
}

void check_dormant() {
  reset_host();
  {
    RoomAggregateHistory history;
    history.setup();
    Model model{};
    usp_room_period_chart::build(history, Period::YEAR_1, day_key(399U),
                                     420, 120, 120, &model);
    assert(model.point_count == 0U);
    assert(model.valid_count == 0U);
    assert(model.gap_count == 0U);
    assert(host_alloc_calls == 0U);
  }
  assert(host_live_bytes == 0);
}

void check_latest_twelve_runs() {
  reset_host();
  {
    RoomAggregateHistory history;
    history.setup();
    uint32_t epoch = 1800000000U;
    for (size_t day = 0; day < 30U; ++day) {
      uint16_t year = 2026;
      uint16_t doy = static_cast<uint16_t>(200U + day);
      const bool valid_day = (day % 2U) == 0U;
      for (uint8_t hour = 0; hour < 24U; ++hour) {
        for (uint8_t slot = 0; slot < 12U; ++slot) {
          assert(history.record(epoch, year, doy, hour, 21.0f, 50.0f, valid_day));
          epoch += 300U;
        }
      }
    }
    assert(history.flush());
    const uint32_t end = RoomAggregateHistory::local_day_key(2026, 229U);
    Model model{};
    usp_room_period_chart::build(history, Period::DAYS_30, end,
                                     420, 120, 120, &model);
    assert(model.segment_count == 12U);
    assert(model.dropped_runs == 3U);
    assert(model.point_count == 12U);
    check_bounds(model);
  }
  assert(host_live_bytes == 0);
}

}  // namespace

int main() {
  static_assert(usp_room_chart::MAX_POINTS == 372U);
  check_dormant();
  check_main_windows();
  check_latest_twelve_runs();
  std::cout
      << "ROOM_PERIOD_PREFLIGHT=PASS 7d167/2 30d29/2 1y365/2 "
      << "counts32=105108 gaps300 latest12 dropped3 maxpoints372 dormant0"
      << " model_bytes=" << sizeof(Model) << "\n";
}
