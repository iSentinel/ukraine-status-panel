#include "usp_room_chart.h"
#include <cassert>
#include <iostream>
using esphome::usp_room_history::RoomHistory;
static constexpr uint32_t BASE = 1800000000;
static void boundary() {
  assert(!usp_room_chart::begins_run(true, BASE, BASE + 599));
  assert(!usp_room_chart::begins_run(true, BASE, BASE + 600));
  assert(usp_room_chart::begins_run(true, BASE, BASE + 601));
  assert(usp_room_chart::begins_run(true, BASE, BASE + 900));
}
static void segments() {
  RoomHistory history;
  history.setup();
  history.record(BASE, 20, 50, true);
  history.record(BASE + 600, 22, 60, true);
  history.record(BASE + 1500, 24, 70, true);
  usp_room_chart::Model model;
  usp_room_chart::build(history, BASE + 1500, 420, 120, 120, &model);
  assert(model.segment_count == 2 && model.point_count == 3);
  assert(model.segments[0].count == 2 && model.segments[1].count == 1);
  for (unsigned i = 0; i < model.point_count; ++i) {
    assert(model.temperature[i].x >= 0 && model.temperature[i].x <= 420);
    assert(model.temperature[i].y >= 0 && model.temperature[i].y <= 120);
  }
  history.clear();
  for (unsigned i = 0; i < 14; ++i) {
    history.record(BASE + i * 900, 20, 50, true);
    history.record(BASE + i * 900 + 300, 0, 0, false);
  }
  usp_room_chart::build(history, BASE + 12000, 420, 120, 120, &model);
  assert(model.segment_count == 12 && model.dropped_runs == 2);
  assert(model.point_count == 12);
}
int main() {
  boundary();
  segments();
  room_test_heap::release();
  std::cout << "ROOM_CHART_PRODUCTION=PASS\n";
}
