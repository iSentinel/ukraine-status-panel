#include "refresh_profile.h"
#include <cassert>
#include <iostream>

using esphome::lvgl::ProfileLogWindow;
using esphome::lvgl::RefreshProfile;

void complete_refresh(uint32_t base) {
  RefreshProfile profile;
  profile.begin(base);
  profile.refresh_start(base + 4);
  profile.layout_ready(base + 9);
  profile.layout_ready(base + 10);
  profile.render_start(base + 12);
  profile.render_start(base + 15);
  profile.ready(base + 20);
  const auto sample = profile.end(base + 23);
  assert(sample.handler_ms == 23 && sample.pre_ms == 12);
  assert(sample.render_ms == 8 && sample.post_ms == 3);
  assert(sample.dispatch_ms == 4 && sample.layout_ms == 6 && sample.prepare_ms == 2);
  assert(sample.ready_seen && sample.render_seen);
}

void incomplete_refreshes() {
  RefreshProfile profile;
  profile.ready(9);  // Events outside the handler cannot affect its sample.
  profile.begin(10);
  profile.render_start(15);
  const auto partial = profile.end(25);
  assert(partial.render_ms == 10 && partial.post_ms == 0 && !partial.ready_seen);
  assert(partial.pre_ms + partial.render_ms + partial.post_ms == partial.handler_ms);
  profile.ready(100);
  profile.begin(200);
  profile.refresh_start(201);
  profile.layout_ready(203);
  profile.ready(205);  // LVGL can signal ready without drawing invalid areas.
  const auto empty = profile.end(207);
  assert(!empty.render_seen && !empty.ready_seen && empty.pre_ms == 7);
  assert(empty.dispatch_ms == 1 && empty.layout_ms == 2 && empty.prepare_ms == 4);
}

void multiple_refreshes() {
  RefreshProfile profile;
  profile.begin(0);
  profile.refresh_start(1);
  profile.layout_ready(3);
  profile.render_start(5);
  profile.ready(9);
  profile.refresh_start(10);
  profile.layout_ready(12);
  profile.render_start(14);
  profile.ready(18);
  const auto sample = profile.end(20);
  assert(sample.pre_ms == 5 && sample.render_ms == 13 && sample.post_ms == 2);
  assert(sample.layout_ms == 2 && sample.prepare_ms == 2);
  profile.begin(30);
  profile.render_start(31);
  profile.ready(33);
  profile.render_start(35);
  const auto unfinished = profile.end(40);
  assert(!unfinished.ready_seen && unfinished.render_ms == 9 && unfinished.post_ms == 0);

}

void log_boundaries() {
  ProfileLogWindow window;
  for (uint32_t i = 0; i < 70000; ++i) {
    assert(window.observe(i, 0, 50, 8) == (i < 8));
    assert(window.sequence == i);
  }
  assert(window.observe(70000, 50, 50, 8));
  assert(!window.observe(70999, 50, 50, 8));
  assert(window.observe(71000, 50, 50, 8));
  ProfileLogWindow wrap;
  assert(wrap.observe(UINT32_MAX - 10, 50, 50, 0));
  assert(!wrap.observe(20, 50, 50, 0));
  assert(wrap.observe(1000, 50, 50, 0));
}

int main() {
  complete_refresh(0);
  complete_refresh(UINT32_MAX - 10);
  incomplete_refreshes();
  multiple_refreshes();
  log_boundaries();
  std::cout << "PASS native profiler: rollover, missing ready, multiple refreshes, 70000 calls, rate limit\n";
}
