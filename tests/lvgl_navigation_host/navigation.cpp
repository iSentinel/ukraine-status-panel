#include "usp_lcd_navigation_controller.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>

uint32_t navigation_test_tick = 1000;
unsigned trace_calls = 0;
bool trace_raw = false, trace_press = false, trace_release = false;

void capture_trace(const char *stage, const usp_lcd_navigation::TouchTraceRecord &record) {
  ++trace_calls;
  trace_raw |= std::strcmp(stage, "GT911_RELEASE") == 0;
  trace_press |= std::strcmp(stage, "LVGL_PRESS") == 0;
  trace_release |= std::strcmp(stage, "LVGL_RELEASE") == 0;
  assert(record.pointers == 1 && record.bindings == 1);
}


/** Reproduce the cached GT911 listener and its LVGL timer input. */
struct PhysicalPointer {
  lv_indev_t *input{};
  lv_point_t point{};
  bool pressed{};
  usp_lcd_navigation::Controller *controller{};
  uint32_t raw_elapsed{};

  explicit PhysicalPointer(lv_display_t *display) {
    input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input, display);
    lv_indev_set_user_data(input, this);
    lv_indev_set_read_cb(input, [](lv_indev_t *device, lv_indev_data_t *data) {
      auto *self = static_cast<PhysicalPointer *>(lv_indev_get_user_data(device));
      data->state = self->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
      // The actual LVTouchListener does not assign coordinates on release.
      if (self->pressed) data->point = self->point;
    });
  }

  void raw_update_() {
    if (controller != nullptr && pressed)
      controller->raw_touch_point(point.x, point.y, navigation_test_tick);
  }

  void elapse(uint32_t duration, bool process_lvgl) {
    for (uint32_t elapsed = 0; elapsed < duration; elapsed += 10) {
      navigation_test_tick += 10;
      lv_tick_inc(10);
      raw_elapsed += 10;
      if (raw_elapsed >= 50) {
        raw_elapsed = 0;
        raw_update_();
      }
      if (process_lvgl) lv_timer_handler();
    }
  }

  void advance(uint32_t duration) { this->elapse(duration, true); }
  void wait_without_lvgl(uint32_t duration) { this->elapse(duration, false); }

  void set_state(int16_t x, int16_t y, bool down) {
    const bool was_pressed = pressed;
    point = {x, y};
    if (controller != nullptr) {
      if (down && !was_pressed)
        controller->raw_touch_press(x, y, navigation_test_tick);
      else if (down)
        controller->raw_touch_point(x, y, navigation_test_tick);
      else if (was_pressed)
        controller->raw_touch_release(navigation_test_tick);
    }
    pressed = down;
  }

  void feed(int16_t x, int16_t y, bool down, uint32_t duration = 40) {
    this->set_state(x, y, down);
    this->advance(duration);
  }

  ~PhysicalPointer() { lv_indev_delete(input); }
};

struct Fixture {
  lv_display_t *display{};
  lv_obj_t *card{};
  lv_obj_t *header{};
  lv_obj_t *menu{};
  usp_lcd_navigation::Controller controller;
  PhysicalPointer *physical{};
  uint16_t pixels[1024 * 20]{};

  Fixture(bool scrolling, bool real_timer) {
    display = lv_display_create(1024, 600);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t *d, const lv_area_t *,
                                       uint8_t *) { lv_display_flush_ready(d); });
    auto *screen = lv_display_get_screen_active(display);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    card = lv_obj_create(screen);
    lv_obj_set_pos(card, 100, 100);
    lv_obj_set_size(card, 300, 160);
    lv_obj_set_style_pad_all(card, 0, 0);
    if (!scrolling) lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    auto *overflow = lv_obj_create(card);
    lv_obj_set_pos(overflow, 10, 150);
    lv_obj_set_size(overflow, 100, 40);
    header = lv_obj_create(lv_display_get_layer_top(display));
    lv_obj_set_size(header, 300, 64);
    menu = lv_obj_create(lv_display_get_layer_top(display));
    lv_obj_add_flag(menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(screen);
    // Firmware creates the physical input before the boot-time attach.
    if (real_timer) {
      physical = new PhysicalPointer(display);
      physical->controller = &controller;
    }
    controller.attach(header, menu);
    controller.set_fullscreen(true);
    controller.set_page(0);
  }

  ~Fixture() {
    controller.qa_end();
    delete physical;
    lv_display_delete(display);
  }

  void point(int16_t x, int16_t y, bool pressed, uint32_t duration = 80) {
    if (physical) {
      physical->feed(x, y, pressed, duration);
    } else {
      controller.qa_touch(x, y, pressed);
      navigation_test_tick += duration;
      lv_tick_inc(duration);
    }
  }

  void drag() {
    point(350, 160, true);
    point(280, 160, true);
    point(200, 160, true);
    point(200, 160, false);
  }

  void hold() {
    point(150, 30, true, 1200);
    point(150, 30, false);
  }
};

void check_swipe(bool scrolling, bool timer) {
  Fixture fixture(scrolling, timer);
  fixture.drag();
  const auto action = fixture.controller.take_action();
  std::printf("scrolling=%d timer=%d action=%u consumed=%d\n",
              int(scrolling), int(timer), unsigned(action),
              int(fixture.controller.consumed()));
  assert(action == (scrolling ? usp_lcd_gesture::Action::NONE :
                              usp_lcd_gesture::Action::NEXT));
}

void check_hold() {
  Fixture fixture(false, true);
  fixture.hold();
  const auto action = fixture.controller.take_action();
  std::printf("physical_timer_header action=%u\n", unsigned(action));
  assert(action == usp_lcd_gesture::Action::MENU);
}

void check_recorded_physical_stroke() {
  Fixture fixture(false, true);
  lv_obj_set_pos(fixture.card, 0, 70);
  lv_obj_set_size(fixture.card, 1024, 516);
  lv_obj_update_layout(lv_display_get_screen_active(fixture.display));
  fixture.point(15, 290, true, 60);
  fixture.point(100, 285, true, 80);
  fixture.point(281, 271, true, 140);
  fixture.point(281, 271, false);
  const auto action = fixture.controller.take_action();
  assert(action == usp_lcd_gesture::Action::PREVIOUS);
  assert(usp_lcd_gesture::adjacent_page(0, action) == 9);
  assert(fixture.controller.consumed());
  std::puts("RECORDED_GT911_STROKE_REPLAY=PASS Home_to_Clock");
}

void check_stale_physical_timing() {
  Fixture fixture(false, true);
  fixture.physical->set_state(350, 160, true);
  fixture.physical->wait_without_lvgl(800);
  fixture.physical->advance(20);
  fixture.physical->wait_without_lvgl(80);
  fixture.physical->set_state(200, 160, true);
  fixture.physical->advance(20);
  fixture.physical->set_state(200, 160, false);
  fixture.physical->advance(20);
  const auto action = fixture.controller.take_action();
  std::printf("STALE_GT911_STROKE raw_ms>=900 action=%u consumed=%d\n", unsigned(action), int(fixture.controller.consumed()));
  assert(action == usp_lcd_gesture::Action::NONE);
  std::puts("TOUCH_EVENT_TIME_LVGL=PASS stale_physical_swipe_rejected=1");
}

void check_trace() {
  Fixture fixture(false, true);
  fixture.controller.trace().set_sink(capture_trace);
  fixture.drag();
  assert(trace_calls == 0);  // Diagnostics are off after boot.
  fixture.controller.trace_for(2);
  fixture.drag();
  assert(trace_raw && trace_press && trace_release);
  const auto count = trace_calls;
  fixture.physical->advance(2100);
  fixture.drag();
  assert(trace_calls == count);  // Time limit expires without another command.
  fixture.controller.trace_for(0);
  fixture.drag();
  assert(trace_calls == count);
  std::puts("TOUCH_TRACE=PASS default_off bounded raw_and_lvgl");
}

int main() {
  lv_init();
  for (bool timer : {false, true}) {
    check_swipe(false, timer);
    check_swipe(true, timer);
  }
  check_hold();
  check_stale_physical_timing();
  check_trace();
  check_recorded_physical_stroke();
  lv_deinit();
  std::puts("LVGL_NAVIGATION_INTEGRATION=PASS physical_timer_model=PASS");
}
