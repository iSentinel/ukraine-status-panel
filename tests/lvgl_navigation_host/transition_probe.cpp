#include "usp_transition_probe.h"
#include <cassert>
#include <cstdio>

uint32_t navigation_test_tick = 1000;

void tick(uint32_t duration) {
  for (uint32_t index = 0; index < duration; index += 5) {
    navigation_test_tick += 5;
    lv_timer_handler();
  }
}

void exercise(lv_display_t *display, lv_obj_t *target, bool animated) {
  auto &probe = usp_transition::probe();
  const uint32_t callbacks = lv_display_get_event_count(display);
  assert(probe.begin(display, target, 3, animated));
  assert(!probe.begin(display, target, 3, animated));  // No overlapping window.
  lv_screen_load_anim(target, animated ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT :
                      LV_SCREEN_LOAD_ANIM_NONE, animated ? 350 : 0, 0, false);
  tick(800);
  assert(probe.sample().loaded && probe.sample().complete);
  assert(probe.sample().frames >= (animated ? 2u : 1u));
  const auto frames = probe.sample().frames;
  tick(100);
  assert(probe.sample().frames == frames);  // Ignore updates after final frame.
  probe.finish();
  probe.finish();  // Cleanup is safe without a second report or callback.
  assert(!probe.active() && lv_display_get_event_count(display) == callbacks);
  assert(!probe.begin(display, target, 3, animated));  // Same page is not a test.
}

int main() {
  lv_init();
  lv_tick_set_cb([]() { return navigation_test_tick; });
  static uint16_t pixels[1024 * 20];
  auto *display = lv_display_create(1024, 600);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels),
                        LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
    lv_display_flush_ready(d);
  });
  auto *first = lv_obj_create(nullptr);
  auto *second = lv_obj_create(nullptr);
  tick(50);
  exercise(display, first, false);
  exercise(display, second, true);
  navigation_test_tick = UINT32_MAX - 100;  // Unsigned elapsed time may wrap.
  exercise(display, first, true);
  lv_display_delete(display);
  lv_deinit();
  std::puts("TRANSITION_HOST=PASS real_LVGL cleanup overlap instant slide rollover");
}
